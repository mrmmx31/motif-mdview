#define _POSIX_C_SOURCE 200809L
#include "md_document.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { const char *s; size_t n; } Slice;
typedef struct { MdDocument *d; char *err; size_t errn; int failed; } Parser;
static void error_set(char *e, size_t n, const char *message) {
 if (n) snprintf(e, n, "%s", message);
}
static int fail(Parser *p, const char *message) {
 if (!p->failed) error_set(p->err, p->errn, message);
 p->failed = 1;
 return 0;
}
void md_free(MdDocument *d) {
 if (!d) return;
 free(d->text); free(d->spans); memset(d, 0, sizeof(*d));
}
static int emit(Parser *p, const char *s, size_t n, unsigned style) {
 MdDocument *d = p->d;
 if (p->failed || !n) return !p->failed;
 if (n > MDV_MAX_INPUT * 4u - d->length) return fail(p, "Texto renderizado excede o limite de 32 MiB.");
 size_t need = d->length + n + 1;
 if (need > d->capacity) {
  size_t cap = d->capacity ? d->capacity : 1024;
  while (cap < need) cap *= 2;
  char *t = realloc(d->text, cap);
  if (!t) return fail(p, "Memória insuficiente.");
  d->text = t; d->capacity = cap;
 }
 size_t start = d->length;
 memcpy(d->text + start, s, n); d->length += n; d->text[d->length] = '\0';
 if (style) {
  if (d->span_count && d->spans[d->span_count-1].end == start &&
      d->spans[d->span_count-1].style == style) {
   d->spans[d->span_count-1].end = d->length;
  } else {
   if (d->span_count >= MDV_MAX_SPANS) return fail(p, "Documento contém mais de 200.000 trechos formatados.");
   if (d->span_count == d->span_capacity) {
    size_t cap = d->span_capacity ? d->span_capacity * 2 : 64;
    MdSpan *v = realloc(d->spans, cap * sizeof(*v));
    if (!v) return fail(p, "Memória insuficiente.");
    d->spans = v; d->span_capacity = cap;
   }
   d->spans[d->span_count++] = (MdSpan){start, d->length, style};
  }
 }
 return 1;
}
static int lit(Parser *p, const char *s, unsigned style) {
 return emit(p, s, strlen(s), style);
}
static size_t utf8_len(const unsigned char *s, size_t n) {
 if (!n) return 0;
 unsigned char c=s[0];
 if (c < 128) return 1;
 size_t len = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
 if (!len || len>n) return 0;
 for(size_t i=1; i<len; i++) if((s[i]&0xc0)!=0x80) return 0;
 if (len==3 && ((c==0xe0 && s[1]<0xa0) || (c==0xed && s[1]>=0xa0))) return 0;
 if (len==4 && ((c==0xf0 && s[1]<0x90) || (c==0xf4 && s[1]>=0x90))) return 0;
 return len;
}
/* Escape controls and replace malformed UTF-8 before any display backend sees it. */
static char *sanitize(const unsigned char *in, size_t n, size_t *outn, Parser *p) {
 char *out = malloc(n * 4 + 1);
 if (!out) { fail(p,"Memória insuficiente."); return NULL; }
 size_t j=0;
 for(size_t i=0;i<n;) {
  unsigned char c=in[i];
  if (c==0) {free(out); fail(p,"O arquivo contém bytes NUL; esperado texto UTF-8."); return NULL;}
  if (c=='\r' && i+1<n && in[i+1]=='\n') {i++;continue;}
  if ((c<32 && c!='\n' && c!='\t') || c==127) {
   out[j++]='^'; out[j++]=c==127 ? '?' : (char)(c+64); i++; continue;
  }
  if (c==0xc2 && i+1<n && in[i+1]>=0x80 && in[i+1]<=0x9f) {
   out[j++]='?'; i+=2; continue;
  }
  size_t k=utf8_len(in+i,n-i);
  if (!k) {memcpy(out+j,"\xef\xbf\xbd",3);j+=3;i++;}
  else {memcpy(out+j,in+i,k);j+=k;i+=k;}
 }
 out[j]='\0'; *outn=j; return out;
}
static size_t find_sub(Slice s, size_t start, const char *needle, size_t n) {
 if (n>s.n) return s.n;
 for(size_t i=start;i<=s.n-n;i++) {
  if (s.s[i]=='\\') {i++;continue;}
  if (!memcmp(s.s+i,needle,n)) return i;
 }
 return s.n;
}
static int inline_text(Parser *p, Slice s, unsigned style, unsigned depth) {
 if (depth>12) return emit(p,s.s,s.n,style);
 int links_possible=1;
 for(size_t i=0;i<s.n && !p->failed;) {
  if(s.s[i]=='\\' && i+1<s.n && ispunct((unsigned char)s.s[i+1])) {
   emit(p,s.s+i+1,1,style);i+=2;continue;
  }
  if(s.s[i]=='\t') {lit(p,"    ",style);i++;continue;}
  if(s.s[i]=='`') {
   size_t run=1;while(i+run<s.n && s.s[i+run]=='`') run++;
   size_t end=find_sub(s,i+run,s.s+i,run);
   if(end<s.n && end>i+run) {emit(p,s.s+i+run,end-i-run,style|MD_CODE);i=end+run;continue;}
   emit(p,s.s+i,run,style);i+=run;continue;
  }
  int image = s.s[i]=='!' && i+1<s.n && s.s[i+1]=='[';
  if (links_possible && (s.s[i]=='[' || image)) {
   size_t begin=i+(image?2:1), close=find_sub(s,begin,"](",2);
   if(close<s.n) {
    size_t end=find_sub(s,close+2,")",1);
    if(end==s.n)links_possible=0;
    /* Nested parentheses and optional link titles remain literal. */
    int valid=end<s.n && end>close+2;
    if(valid) for(size_t k=close+2;k<end;k++) if(s.s[k]=='(' || s.s[k]=='"' || s.s[k]=='\'') valid=0;
    if(valid) {
     if(image) lit(p,"[imagem: ",style);
     inline_text(p,(Slice){s.s+begin,close-begin},style|(image?0:MD_LINK),depth+1);
     if(image) lit(p,"]",style);
     lit(p," <",style|MD_LINK);
     emit(p,s.s+close+2,end-close-2,style|MD_LINK);
     lit(p,">",style|MD_LINK);i=end+1;continue;
    }
    if(end<s.n){emit(p,s.s+i,end+1-i,style);i=end+1;continue;}
   } else links_possible=0;
  }
  if(s.s[i]=='*' || s.s[i]=='_') {
   char c=s.s[i]; size_t run=i+1<s.n && s.s[i+1]==c ? 2 : 1;
   int boundary=c!='_' || i==0 || !isalnum((unsigned char)s.s[i-1]);
   if(boundary && i+run<s.n && !isspace((unsigned char)s.s[i+run])) {
    size_t end=find_sub(s,i+run,s.s+i,run);
    if(end<s.n && end>i+run && !isspace((unsigned char)s.s[end-1])) {
     inline_text(p,(Slice){s.s+i+run,end-i-run},style|(run==2?MD_BOLD:MD_ITALIC),depth+1);
     i=end+run; continue;
    }
   }
  }
  size_t k=utf8_len((const unsigned char*)s.s+i,s.n-i);
  if(!k) k=1;
  emit(p,s.s+i,k,style);i+=k;
 }
 return !p->failed;
}
static Slice trim(Slice s) {
 while(s.n && (s.s[0]==' ' || s.s[0]=='\t')) {s.s++;s.n--;}
 while(s.n && (s.s[s.n-1]==' ' || s.s[s.n-1]=='\t')) s.n--;
 return s;
}
static int rule(Slice s) {
 s=trim(s);if(s.n<3) return 0;
 char c=s.s[0]; if(c!='-'&&c!='*'&&c!='_')return 0;
 size_t count=0;
 for(size_t i=0;i<s.n;i++) {if(s.s[i]==c) count++;else if(s.s[i]!=' '&&s.s[i]!='\t')return 0;}
 return count>=3;
}
static int setext(Slice s) {
 s=trim(s); if(!s.n || (s.s[0]!='=' && s.s[0]!='-'))return 0;
 char c=s.s[0]; for(size_t i=0;i<s.n;i++) if(s.s[i]!=c)return 0;
 return c=='='?1:2;
}
static size_t fence(Slice s, char *kind) {
 size_t indent=0;while(indent<s.n && s.s[indent]==' ')indent++;
 if(indent>3 || indent==s.n)return 0;
 char c=s.s[indent]; if(c!='`'&&c!='~')return 0;
 size_t n=0;while(indent+n<s.n && s.s[indent+n]==c)n++;
 if(n<3)return 0;
 *kind=c;return n;
}
static int render_lines(Parser *p, Slice *lines, size_t count) {
 char fkind=0;size_t flen=0;
 for(size_t i=0;i<count && !p->failed;i++) {
  Slice s=lines[i]; char k=0; size_t len=fence(s,&k);
  if(flen) {
   Slice t=trim(s);
   if(len>=flen && k==fkind && trim((Slice){t.s+len,t.n-len}).n==0) {flen=0;lit(p,"\n",0);continue;}
   for(size_t j=0;j<s.n;j++) if(s.s[j]=='\t')lit(p,"    ",MD_CODE);else emit(p,s.s+j,1,MD_CODE);
   lit(p,"\n",MD_CODE);continue;
  }
  if(len) {
   flen=len;fkind=k;Slice t=trim(s);Slice info=trim((Slice){t.s+len,t.n-len});
   lit(p,"[código",MD_CODE);if(info.n){lit(p,": ",MD_CODE);emit(p,info.s,info.n,MD_CODE);}lit(p,"]\n",MD_CODE);continue;
  }
  size_t indent=0;while(indent<s.n && s.s[indent]==' ')indent++;
  Slice t={s.s+indent,s.n-indent};unsigned style=0;
  /* Setext only for a single unindented ordinary line. */
  if(s.n && indent==0 && i+1<count && s.s[0]!='>' && s.s[0]!='#' &&
     !(s.n>1 && strchr("-*+",s.s[0]) && s.s[1]==' ')) {
   int h=setext(lines[i+1]);
   if(h){inline_text(p,s,h==1?MD_H1:MD_H2,0);lit(p,"\n\n",0);i++;continue;}
  }
  if(indent<=3 && rule(s)){lit(p,"────────────────────────────────\n",MD_RULE);continue;}
  if(indent<=3 && t.n && t.s[0]=='#') {
   size_t h=0;while(h<t.n && t.s[h]=='#')h++;
   if(h<=6 && (h==t.n || t.s[h]==' ' || t.s[h]=='\t')) {
    t=trim((Slice){t.s+h,t.n-h});
    size_t z=t.n;while(z && t.s[z-1]=='#')z--;
    if(z<t.n && (z==0 || t.s[z-1]==' ' || t.s[z-1]=='\t')) t=trim((Slice){t.s,z});
    inline_text(p,t,h==1?MD_H1:h==2?MD_H2:MD_H3,0);lit(p,"\n\n",0);continue;
   }
  }
  if(indent<=3 && t.n && t.s[0]=='>') {
   style|=MD_QUOTE;lit(p,"│ ",style);t.s++;t.n--;if(t.n&&t.s[0]==' '){t.s++;t.n--;}
  } else {
   emit(p,s.s,indent,0);
   if(t.n>1 && strchr("-*+",t.s[0]) && (t.s[1]==' '||t.s[1]=='\t')) {
    lit(p,"• ",0);t.s+=2;t.n-=2;
   }
  }
  inline_text(p,t,style,0);lit(p,"\n",0);
 }
 return !p->failed;
}
int md_parse(const unsigned char *input, size_t length, MdDocument *out, char *error, size_t error_size) {
 memset(out,0,sizeof(*out)); if(error_size)error[0]='\0';
 Parser p={out,error,error_size,0};
 if(length>MDV_MAX_INPUT)return fail(&p,"Arquivo excede o limite de 8 MiB.");
 if(!input && length)return fail(&p,"Entrada inválida.");
 out->source_bytes=length;
 if(length>=3 && input[0]==0xef && input[1]==0xbb && input[2]==0xbf){input+=3;length-=3;}
 size_t n=0;char *clean=sanitize(input,length,&n,&p);if(!clean){md_free(out);return 0;}
 size_t count=0;for(size_t i=0;i<n;i++)if(clean[i]=='\n')count++;
 if(n && clean[n-1]!='\n')count++;
 if(count>MDV_MAX_LINES){free(clean);md_free(out);return fail(&p,"Arquivo contém mais de 200.000 linhas.");}
 Slice *lines=calloc(count?count:1,sizeof(*lines));
 if(!lines){free(clean);md_free(out);return fail(&p,"Memória insuficiente.");}
 size_t start=0,index=0;
 for(size_t i=0;i<n;i++)if(clean[i]=='\n'){lines[index++]=(Slice){clean+start,i-start};start=i+1;}
 if(start<n)lines[index++]=(Slice){clean+start,n-start};
 out->source_lines=count;render_lines(&p,lines,count);free(lines);free(clean);
 if(!out->text && !p.failed){out->text=calloc(1,1);out->capacity=1;if(!out->text)fail(&p,"Memória insuficiente.");}
 if(p.failed){md_free(out);return 0;}return 1;
}
int md_parse_raw(const unsigned char *input, size_t length, MdDocument *out, char *error, size_t error_size) {
 memset(out,0,sizeof(*out));if(error_size)error[0]='\0';
 Parser p={out,error,error_size,0};
 if(length>MDV_MAX_INPUT)return fail(&p,"Arquivo excede o limite de 8 MiB.");
 if(!input && length)return fail(&p,"Entrada inválida.");
 out->source_bytes=length;
 if(length>=3 && input[0]==0xef && input[1]==0xbb && input[2]==0xbf){input+=3;length-=3;}
 size_t n=0;char *clean=sanitize(input,length,&n,&p);if(!clean){md_free(out);return 0;}
 size_t lines=0;for(size_t i=0;i<n;i++)if(clean[i]=='\n')lines++;
 if(n && clean[n-1]!='\n')lines++;
 if(lines>MDV_MAX_LINES){free(clean);md_free(out);return fail(&p,"Arquivo contém mais de 200.000 linhas.");}
 out->source_lines=lines;
 emit(&p,clean,n,MD_CODE);free(clean);
 if(!out->text && !p.failed){out->text=calloc(1,1);out->capacity=1;if(!out->text)fail(&p,"Memória insuficiente.");}
 if(p.failed){md_free(out);return 0;}return 1;
}
int md_load_mode(const char *path, int raw, MdDocument *out, char *error, size_t error_size) {
 memset(out,0,sizeof(*out));if(error_size)error[0]='\0';
 int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
 FILE *f=fd<0?NULL:fdopen(fd,"rb");
 if(fd>=0 && !f)close(fd);
 if(!f){if(error_size)snprintf(error,error_size,"Não foi possível abrir o arquivo: %s",strerror(errno));return 0;}
 struct stat st;
 if(fstat(fileno(f),&st)!=0 || !S_ISREG(st.st_mode)){fclose(f);error_set(error,error_size,"Selecione um arquivo local regular.");return 0;}
 if(st.st_size<0 || (uintmax_t)st.st_size>MDV_MAX_INPUT){fclose(f);error_set(error,error_size,"Arquivo excede o limite de 8 MiB.");return 0;}
 size_t cap=(size_t)st.st_size+1;
 unsigned char *input=malloc(cap);
 if(!input){fclose(f);error_set(error,error_size,"Memória insuficiente.");return 0;}
 size_t n=fread(input,1,cap,f);int failed=ferror(f);int extra=fgetc(f);failed|=ferror(f);fclose(f);
 if(failed || extra!=EOF || n==cap){free(input);error_set(error,error_size,"Arquivo mudou durante a leitura; tente recarregar.");return 0;}
 int ok=raw?md_parse_raw(input,n,out,error,error_size):md_parse(input,n,out,error,error_size);free(input);return ok;
}
int md_load(const char *path, MdDocument *out, char *error, size_t error_size) {
 return md_load_mode(path,0,out,error,error_size);
}
