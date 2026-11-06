#define _POSIX_C_SOURCE 200809L
#include "md_document.h"
#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <Xm/FileSB.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/MainW.h>
#include <Xm/MessageB.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrollBar.h>
#include <Xm/ToggleB.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <limits.h>
#include <stdint.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { size_t begin,end; int height; } Row;
typedef struct {
 XtAppContext context;
 Widget top,main,canvas,scroll,status,raw_toggle,chooser;
 Display *display;
 int screen,quit,points,raw;
 XftFont *fonts[7];
 int widths[7][128];
 XftColor ink,paper,muted,accent,codebg;
 int colors_ready;
 MdDocument doc;
 Row *rows;
 size_t row_count,row_capacity,top_row;
 char *path;
} App;
static App app;
static void draw(void);
static void relayout(void);
static void open_file(const char *path);
static void error_dialog(const char *text);
static void label_text(Widget w,const char *text) {
 XmString s=XmStringCreateLocalized((char*)text);
 XtVaSetValues(w,XmNlabelString,s,NULL);XmStringFree(s);
}
static void destroy_dialog(Widget w,XtPointer client,XtPointer call) {
 (void)w;(void)call;XtDestroyWidget((Widget)client);
}
static void error_dialog(const char *text) {
 XmString s=XmStringCreateLocalized((char*)text);
 Arg args[3]; Cardinal n=0;
 XtSetArg(args[n],XmNmessageString,s);n++;
 XtSetArg(args[n],XmNdialogStyle,XmDIALOG_FULL_APPLICATION_MODAL);n++;
 Widget d=XmCreateErrorDialog(app.top,"erro",args,n);XmStringFree(s);
 XtUnmanageChild(XmMessageBoxGetChild(d,XmDIALOG_CANCEL_BUTTON));
 XtUnmanageChild(XmMessageBoxGetChild(d,XmDIALOG_HELP_BUTTON));
 XtAddCallback(d,XmNokCallback,destroy_dialog,d);XtManageChild(d);
}
static unsigned style_at(size_t byte) {
 size_t lo=0,hi=app.doc.span_count;
 while(lo<hi){size_t mid=lo+(hi-lo)/2;if(app.doc.spans[mid].end<=byte)lo=mid+1;else hi=mid;}
 return lo<app.doc.span_count && app.doc.spans[lo].start<=byte ? app.doc.spans[lo].style:0;
}
static int font_index(unsigned style) {
 if(style&MD_H1)return 4;
 if(style&MD_H2)return 5;
 if(style&MD_H3)return 6;
 if(style&MD_CODE)return 3;
 if(style&MD_BOLD)return 1;
 if(style&MD_ITALIC)return 2;
 return 0;
}
static size_t rune_len(const char *s,size_t remain) {
 unsigned char c=(unsigned char)*s;
 size_t n=c<128?1:c<224?2:c<240?3:4;
 return n<=remain?n:1;
}
static int rune_width(int f,const char *s,size_t n) {
 if(n==1 && *s=='\t')return app.widths[f][' ']*4;
 if(n==1 && (unsigned char)*s<128)return app.widths[f][(unsigned char)*s];
 XGlyphInfo ext;XftTextExtentsUtf8(app.display,app.fonts[f],(const FcChar8*)s,(int)n,&ext);return ext.xOff;
}
static int load_fonts(void) {
 const char *families[]={"DejaVu Sans","DejaVu Sans:style=Bold","DejaVu Sans:style=Oblique","DejaVu Sans Mono","DejaVu Sans:style=Bold","DejaVu Sans:style=Bold","DejaVu Sans:style=Bold"};
 const double scales[]={1,1,1,0.95,1.65,1.35,1.15};
 XftFont *next[7]={0};
 for(int i=0;i<7;i++) {
  char name[128];snprintf(name,sizeof(name),"%s:size=%.1f",families[i],app.points*scales[i]);
  next[i]=XftFontOpenName(app.display,app.screen,name);
  if(!next[i]){for(int j=0;j<i;j++)XftFontClose(app.display,next[j]);return 0;}
 }
 for(int i=0;i<7;i++) {
  if(app.fonts[i])XftFontClose(app.display,app.fonts[i]);
  app.fonts[i]=next[i];
  for(int c=0;c<128;c++){FcChar8 b=(FcChar8)c;XGlyphInfo e;XftTextExtentsUtf8(app.display,next[i],&b,1,&e);app.widths[i][c]=e.xOff;}
 }
 return 1;
}
static void status_update(void) {
 char status[512];
 if(!app.path)snprintf(status,sizeof(status),"Abra um arquivo .md • Somente leitura • Ctrl+O: abrir");
 else snprintf(status,sizeof(status),"%zu bytes • %zu linhas na origem • linha visual %zu/%zu • somente leitura%s",app.doc.source_bytes,app.doc.source_lines,app.row_count?app.top_row+1:0,app.row_count,app.raw?" • RAW":" • Markdown formatado");
 label_text(app.status,status);
}
static void scroll_update(void) {
 int max=(int)(app.row_count?app.row_count:1);
 if(app.top_row>=(size_t)max)app.top_row=(size_t)max-1;
 Dimension h=0;XtVaGetValues(app.canvas,XmNheight,&h,NULL);
 int page=(int)h/(app.fonts[0]->height+7);if(page<1)page=1;
 if(page>max)page=max;
 XtVaSetValues(app.scroll,XmNminimum,0,XmNmaximum,max,XmNsliderSize,1,
  XmNvalue,(int)app.top_row,XmNincrement,1,XmNpageIncrement,page,NULL);
 status_update();
}
static int add_row(size_t start,size_t end,int height) {
 if(app.row_count>=500000)return 0;
 if(app.row_count==app.row_capacity) {
  size_t cap=app.row_capacity?app.row_capacity*2:256;
  Row *r=realloc(app.rows,cap*sizeof(*r));if(!r)return 0;
  app.rows=r;app.row_capacity=cap;
 }
 app.rows[app.row_count++]=(Row){start,end,height};return 1;
}
static size_t row_for_byte(size_t pos) {
 size_t lo=0,hi=app.row_count;
 while(lo<hi){size_t mid=lo+(hi-lo)/2;if(app.rows[mid].end<pos)lo=mid+1;else hi=mid;}
 return lo<app.row_count?lo:app.row_count?app.row_count-1:0;
}
static void relayout(void) {
 if(!app.fonts[0])return;
 size_t keep=app.row_count?app.rows[app.top_row].begin:0;
 Dimension width=0;XtVaGetValues(app.canvas,XmNwidth,&width,NULL);
 int avail=(int)width-48;if(avail<32)avail=32;
 app.row_count=0;size_t pos=0,start=0,last_space=SIZE_MAX;
 int x=0,height=app.fonts[0]->height+7;
 while(pos<app.doc.length) {
  if(app.doc.text[pos]=='\n') {
   if(!add_row(start,pos,height))goto failed;
   pos++;start=pos;last_space=SIZE_MAX;x=0;height=app.fonts[0]->height+7;continue;
  }
  int f=font_index(style_at(pos));size_t n=rune_len(app.doc.text+pos,app.doc.length-pos);
  int w=rune_width(f,app.doc.text+pos,n);
  if(x+w>avail && pos>start) {
   size_t end=last_space!=SIZE_MAX && last_space>start?last_space:pos;
   if(!add_row(start,end,height))goto failed;
   pos=end==last_space?end+1:end;start=pos;last_space=SIZE_MAX;x=0;height=app.fonts[0]->height+7;continue;
  }
  if(app.doc.text[pos]==' ')last_space=pos;
  x+=w;if(app.fonts[f]->height+7>height)height=app.fonts[f]->height+7;
  pos+=n;
 }
 if(start<app.doc.length || !app.row_count)if(!add_row(start,app.doc.length,height))goto failed;
 app.top_row=row_for_byte(keep);scroll_update();draw();return;
failed:
 app.row_count=0;app.top_row=0;status_update();error_dialog("Não foi possível preparar a página. Limite: 500.000 linhas visuais.");draw();
}
static int alloc_color(XftColor *c,const char *name) {
 return XftColorAllocName(app.display,DefaultVisual(app.display,app.screen),DefaultColormap(app.display,app.screen),name,c);
}
static void draw(void) {
 if(!XtIsRealized(app.canvas)||!app.colors_ready)return;
 Dimension width=0,height=0;XtVaGetValues(app.canvas,XmNwidth,&width,XmNheight,&height,NULL);
 XftDraw *d=XftDrawCreate(app.display,XtWindow(app.canvas),DefaultVisual(app.display,app.screen),DefaultColormap(app.display,app.screen));
 if(!d)return;
 XftDrawRect(d,&app.paper,0,0,width,height);
 int y=18;
 if(!app.path) {
  const char *title="Leitor Markdown";const char *hint="Abra um arquivo local pelo botão Abrir ou por Ctrl+O.";
  XftDrawStringUtf8(d,&app.ink,app.fonts[4],24,52,(const FcChar8*)title,(int)strlen(title));
  XftDrawStringUtf8(d,&app.muted,app.fonts[0],24,88,(const FcChar8*)hint,(int)strlen(hint));
 }
 for(size_t r=app.top_row;r<app.row_count && y<(int)height;r++) {
  Row row=app.rows[r];int x=24,ascent=app.fonts[0]->ascent;
  for(size_t p=row.begin;p<row.end;) {int f=font_index(style_at(p));if(app.fonts[f]->ascent>ascent)ascent=app.fonts[f]->ascent;p+=rune_len(app.doc.text+p,row.end-p);}
  for(size_t p=row.begin;p<row.end;) {
   unsigned style=style_at(p);int f=font_index(style);size_t n=rune_len(app.doc.text+p,row.end-p);
   int w=rune_width(f,app.doc.text+p,n);
   if(style&MD_CODE)XftDrawRect(d,&app.codebg,x,y,(unsigned)(w>0?w:1),(unsigned)row.height);
   XftColor *ink=style&MD_LINK?&app.accent:style&(MD_QUOTE|MD_RULE)?&app.muted:&app.ink;
   if(app.doc.text[p]!='\t')XftDrawStringUtf8(d,ink,app.fonts[f],x,y+ascent,(const FcChar8*)app.doc.text+p,(int)n);
   x+=w;p+=n;
  }
  y+=row.height;
 }
 XftDrawDestroy(d);
}
static void redraw_callback(Widget w,XtPointer client,XtPointer call) {(void)w;(void)client;(void)call;draw();}
static void resize_callback(Widget w,XtPointer client,XtPointer call) {(void)w;(void)client;(void)call;relayout();}
static void scroll_callback(Widget w,XtPointer client,XtPointer call) {
 (void)w;(void)client;XmScrollBarCallbackStruct *c=(XmScrollBarCallbackStruct*)call;app.top_row=(size_t)c->value;status_update();draw();
}
static void move_rows(long delta) {
 long next=(long)app.top_row+delta;if(next<0)next=0;
 if(app.row_count && (size_t)next>=app.row_count)next=(long)app.row_count-1;
 app.top_row=(size_t)next;scroll_update();draw();
}
static void open_file(const char *path) {
 MdDocument next;char err[256];
 if(!md_load_mode(path,app.raw,&next,err,sizeof(err))){error_dialog(err);return;}
 char *name=strdup(path);if(!name){md_free(&next);error_dialog("Memória insuficiente.");return;}
 md_free(&app.doc);free(app.path);app.doc=next;app.path=name;app.top_row=0;app.row_count=0;
 const char *base=strrchr(path,'/');base=base?base+1:path;
 char title[512];snprintf(title,sizeof(title),"%s • Leitor Markdown",base);
 XtVaSetValues(app.top,XtNtitle,title,NULL);relayout();
}
static void chooser_ok(Widget w,XtPointer client,XtPointer call) {
 (void)client;XmFileSelectionBoxCallbackStruct *c=(XmFileSelectionBoxCallbackStruct*)call;char *path=NULL;
 if(XmStringGetLtoR(c->value,XmFONTLIST_DEFAULT_TAG,&path)) {XtUnmanageChild(w);open_file(path);XtFree(path);}
}
static void chooser_cancel(Widget w,XtPointer client,XtPointer call) {(void)client;(void)call;XtUnmanageChild(w);}
static void choose_file(Widget w,XtPointer client,XtPointer call) {
 (void)w;(void)client;(void)call;
 if(!app.chooser) {
  Arg args[2];Cardinal n=0;XtSetArg(args[n],XmNdialogStyle,XmDIALOG_FULL_APPLICATION_MODAL);n++;
  app.chooser=XmCreateFileSelectionDialog(app.top,"abrirMarkdown",args,n);
  XtUnmanageChild(XmFileSelectionBoxGetChild(app.chooser,XmDIALOG_HELP_BUTTON));
  XtAddCallback(app.chooser,XmNokCallback,chooser_ok,NULL);
  XtAddCallback(app.chooser,XmNcancelCallback,chooser_cancel,NULL);
 }
 XtManageChild(app.chooser);
}
static void reload_file(Widget w,XtPointer client,XtPointer call) {
 (void)w;(void)client;(void)call;
 if(app.path){char *p=strdup(app.path);if(p){open_file(p);free(p);}else error_dialog("Memória insuficiente.");}
}
static void quit_callback(Widget w,XtPointer client,XtPointer call) {(void)w;(void)client;(void)call;app.quit=1;}
static void raw_callback(Widget w,XtPointer client,XtPointer call) {
 (void)w;(void)client;
 XmToggleButtonCallbackStruct *c=(XmToggleButtonCallbackStruct*)call;
 if(!app.path){app.raw=c->set;return;}
 int previous=app.raw;MdDocument next;char err[256];
 if(!md_load_mode(app.path,c->set,&next,err,sizeof(err))) {
  XmToggleButtonSetState(app.raw_toggle,previous,False);error_dialog(err);return;
 }
 app.raw=c->set;md_free(&app.doc);app.doc=next;app.row_count=0;app.top_row=0;relayout();
}
static void font_callback(Widget w,XtPointer client,XtPointer call) {
 (void)w;(void)call;int delta=(int)(intptr_t)client;
 int old=app.points;app.points+=delta;if(app.points<8)app.points=8;if(app.points>24)app.points=24;
 if(!load_fonts()){app.points=old;error_dialog("Não foi possível carregar as fontes.");return;}relayout();
}
static void key_event(Widget w,XtPointer client,XEvent *event,Boolean *cont) {
 (void)w;(void)client;(void)cont;
 if(event->type==ButtonPress) {
  if(event->xbutton.button==Button4)move_rows(-3);else if(event->xbutton.button==Button5)move_rows(3);
  else XmProcessTraversal(app.canvas,XmTRAVERSE_CURRENT);
  return;
 }
 if(event->type!=KeyPress)return;
 KeySym key=XLookupKeysym(&event->xkey,0);unsigned state=event->xkey.state;
 if(state&ControlMask) {
  if(key==XK_o)choose_file(NULL,NULL,NULL);
  else if(key==XK_r)reload_file(NULL,NULL,NULL);
  else if(key==XK_q)app.quit=1;
  return;
 }
 Dimension height=0;XtVaGetValues(app.canvas,XmNheight,&height,NULL);long page=(long)height/(app.fonts[0]->height+7);if(page<1)page=1;
 if(key==XK_Down || key==XK_j)move_rows(1);
 else if(key==XK_Up || key==XK_k)move_rows(-1);
 else if(key==XK_Page_Down || key==XK_space)move_rows(page);
 else if(key==XK_Page_Up)move_rows(-page);
 else if(key==XK_Home)move_rows(-(long)app.top_row);
 else if(key==XK_End)move_rows((long)app.row_count);
 else if(key==XK_plus || key==XK_equal)font_callback(NULL,(XtPointer)(intptr_t)1,NULL);
 else if(key==XK_minus)font_callback(NULL,(XtPointer)(intptr_t)-1,NULL);
}
static Widget button(Widget parent,const char *name,const char *label,XtCallbackProc callback,XtPointer data) {
 Widget w=XtVaCreateManagedWidget(name,xmPushButtonWidgetClass,parent,NULL);label_text(w,label);
 XtAddCallback(w,XmNactivateCallback,callback,data);return w;
}
static void free_resources(void) {
 md_free(&app.doc);free(app.rows);free(app.path);
 if(app.colors_ready){XftColor *c[]={&app.ink,&app.paper,&app.muted,&app.accent,&app.codebg};for(size_t i=0;i<5;i++)XftColorFree(app.display,DefaultVisual(app.display,app.screen),DefaultColormap(app.display,app.screen),c[i]);}
 for(int i=0;i<7;i++)if(app.fonts[i])XftFontClose(app.display,app.fonts[i]);
}
int main(int argc,char **argv) {
 const char *file=NULL;
 if(argc==2 && !strcmp(argv[1],"--version")){puts("mdv " MDV_VERSION " (Motif + Xft)");return 0;}
 if(argc==2 && !strcmp(argv[1],"--help")){puts("Uso: mdv [--] [arquivo.md]\nLeitor gráfico local em C, Motif e Xft. Somente leitura.");return 0;}
 if(argc>1 && !strcmp(argv[1],"--")){argv++;argc--;}
 if(argc>2){fputs("Abra um arquivo por janela. Uso: mdv [--] [arquivo.md]\n",stderr);return 2;}
 if(argc==2)file=argv[1];
 XtSetLanguageProc(NULL,NULL,NULL);app.points=12;
 int xtargc=1;char *xtargv[]={argv[0],NULL};
 app.top=XtVaAppInitialize(&app.context,"Mdv",NULL,0,&xtargc,xtargv,NULL,
  XtNtitle,"Leitor Markdown",XmNwidth,960,XmNheight,700,XmNdeleteResponse,XmDO_NOTHING,NULL);
 app.display=XtDisplay(app.top);app.screen=DefaultScreen(app.display);
 if(!load_fonts()){fputs("Não foi possível carregar as fontes Xft.\n",stderr);return 1;}
 app.colors_ready=alloc_color(&app.ink,"#25313d") && alloc_color(&app.paper,"#fcfbf8") && alloc_color(&app.muted,"#69747f") && alloc_color(&app.accent,"#176e91") && alloc_color(&app.codebg,"#edf0f2");
 if(!app.colors_ready){fputs("Não foi possível alocar as cores.\n",stderr);free_resources();return 1;}
 app.main=XtVaCreateManagedWidget("main",xmMainWindowWidgetClass,app.top,NULL);
 Widget toolbar=XtVaCreateManagedWidget("toolbar",xmRowColumnWidgetClass,app.main,XmNorientation,XmHORIZONTAL,XmNpacking,XmPACK_TIGHT,NULL);
 button(toolbar,"abrir","Abrir",choose_file,NULL);
 app.raw_toggle=XtVaCreateManagedWidget("raw",xmToggleButtonWidgetClass,toolbar,XmNset,False,NULL);
 label_text(app.raw_toggle,"RAW");XtAddCallback(app.raw_toggle,XmNvalueChangedCallback,raw_callback,NULL);
 Widget form=XtVaCreateManagedWidget("pagina",xmFormWidgetClass,app.main,NULL);
 app.status=XtVaCreateManagedWidget("status",xmLabelWidgetClass,form,XmNalignment,XmALIGNMENT_BEGINNING,XmNleftAttachment,XmATTACH_FORM,XmNrightAttachment,XmATTACH_FORM,XmNbottomAttachment,XmATTACH_FORM,XmNmarginHeight,7,XmNmarginWidth,12,NULL);
 app.scroll=XtVaCreateManagedWidget("rolagem",xmScrollBarWidgetClass,form,XmNorientation,XmVERTICAL,XmNtopAttachment,XmATTACH_FORM,XmNrightAttachment,XmATTACH_FORM,XmNbottomAttachment,XmATTACH_WIDGET,XmNbottomWidget,app.status,NULL);
 app.canvas=XtVaCreateManagedWidget("leitura",xmDrawingAreaWidgetClass,form,XmNbackground,app.paper.pixel,XmNtopAttachment,XmATTACH_FORM,XmNleftAttachment,XmATTACH_FORM,XmNrightAttachment,XmATTACH_WIDGET,XmNrightWidget,app.scroll,XmNbottomAttachment,XmATTACH_WIDGET,XmNbottomWidget,app.status,XmNtraversalOn,True,XmNnavigationType,XmTAB_GROUP,NULL);
 XtAddCallback(app.canvas,XmNexposeCallback,redraw_callback,NULL);XtAddCallback(app.canvas,XmNresizeCallback,resize_callback,NULL);
 XtAddEventHandler(app.canvas,KeyPressMask|ButtonPressMask,False,key_event,NULL);
 XtAddCallback(app.scroll,XmNvalueChangedCallback,scroll_callback,NULL);XtAddCallback(app.scroll,XmNdragCallback,scroll_callback,NULL);
 XmMainWindowSetAreas(app.main,NULL,toolbar,NULL,NULL,form);
 XmAddWMProtocolCallback(app.top,XInternAtom(app.display,"WM_DELETE_WINDOW",False),quit_callback,NULL);
 XtRealizeWidget(app.top);status_update();if(file)open_file(file);
 while(!app.quit)XtAppProcessEvent(app.context,XtIMAll);
 free_resources();XtDestroyWidget(app.top);XtDestroyApplicationContext(app.context);return 0;
}
