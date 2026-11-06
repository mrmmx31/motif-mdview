#include "md_document.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t tests=0;
static void valid(const MdDocument *d) {
 assert(d->text);assert(strlen(d->text)==d->length);
 size_t end=0;
 for(size_t i=0;i<d->span_count;i++) {
  assert(d->spans[i].start>=end);assert(d->spans[i].start<d->spans[i].end);
  assert(d->spans[i].end<=d->length);end=d->spans[i].end;
 }
}
static void check(const char *in,const char *expected,unsigned wanted) {
 MdDocument d;char error[256];assert(md_parse((const unsigned char*)in,strlen(in),&d,error,sizeof(error)));
 valid(&d);if(strcmp(d.text,expected)){fprintf(stderr,"Entrada: %s\nEsperado: %s\nRecebido: %s\n",in,expected,d.text);abort();}
 unsigned styles=0;for(size_t i=0;i<d.span_count;i++)styles|=d.spans[i].style;
 assert((styles&wanted)==wanted);md_free(&d);tests++;
}
int main(void) {
 check("","",0);
 check("# Olá, Steven\n","Olá, Steven\n\n",MD_H1);
 check("## Título ##\n","Título\n\n",MD_H2);
 check("###### Seis\n","Seis\n\n",MD_H3);
 check("####### literal\n","####### literal\n",0);
 check("Título\n====\n","Título\n\n",MD_H1);
 check("Título\n---\n","Título\n\n",MD_H2);
 check("**forte** e *ênfase* e `código`\n","forte e ênfase e código\n",MD_BOLD|MD_ITALIC|MD_CODE);
 check("um_nome_normal\n","um_nome_normal\n",0);
 check("\\*literal\\* e **aberto\n","*literal* e **aberto\n",0);
 check("- item\n  + filho\n1. número\n","• item\n  • filho\n1. número\n",0);
 check("> Citação **forte**\n","│ Citação forte\n",MD_QUOTE|MD_BOLD);
 check("---\n","────────────────────────────────\n",MD_RULE);
 check("```c\nif (*p)\n```\n","[código: c]\nif (*p)\n\n",MD_CODE);
 check("~~~\ntexto\n~~~~\n","[código]\ntexto\n\n",MD_CODE);
 check("```\n**literal**\n","[código]\n**literal**\n",MD_CODE);
 check("[site](https://example.org)\n","site <https://example.org>\n",MD_LINK);
 check("![alt](foto.png)\n","[imagem: alt] <foto.png>\n",MD_LINK);
 check("<script>alert(1)</script>\n","<script>alert(1)</script>\n",0);
 check("| A | B |\n|---|---|\n","| A | B |\n|---|---|\n",0);
 check("\xef\xbb\xbf# ação\r\n","ação\n\n",MD_H1);
 check("x\x1b[31my\n","x^[[31my\n",0);
 check("x\xffy\n","x\xef\xbf\xbdy\n",0);
 check("``````\n","[código]\n",MD_CODE);
 MdDocument d;char error[256];
 unsigned char binary[]={65,0,66};assert(!md_parse(binary,sizeof(binary),&d,error,sizeof(error)));assert(!d.text && !d.spans);tests++;
 assert(!md_parse(NULL,MDV_MAX_INPUT+1u,&d,error,sizeof(error)));tests++;
 const char *raw="# Título\n**negrito**\n\ttexto\n";
 assert(md_parse_raw((const unsigned char*)raw,strlen(raw),&d,error,sizeof(error)));assert(!strcmp(d.text,raw));valid(&d);md_free(&d);tests++;
 assert(md_parse_raw(NULL,0,&d,error,sizeof(error)));assert(!d.length);md_free(&d);tests++;
 char *large=malloc(500001);assert(large);
 memset(large,'[',500000);large[500000]=0;
 assert(md_parse((unsigned char*)large,500000,&d,error,sizeof(error)));valid(&d);md_free(&d);tests++;
 memset(large,'`',500000);large[500000]=0;
 assert(md_parse((unsigned char*)large,500000,&d,error,sizeof(error)));valid(&d);md_free(&d);tests++;
 memset(large,'\n',MDV_MAX_LINES+1);assert(!md_parse((unsigned char*)large,MDV_MAX_LINES+1,&d,error,sizeof(error)));assert(!d.text);tests++;
 free(large);
 uint32_t rng=0x12345678;unsigned char fuzz[2048];
 for(int i=0;i<20000;i++) {
  rng=rng*1664525u+1013904223u;size_t n=rng%sizeof(fuzz);
  for(size_t j=0;j<n;j++){rng=rng*1664525u+1013904223u;fuzz[j]=(unsigned char)(rng>>24);}
  if(md_parse(fuzz,n,&d,error,sizeof(error))){valid(&d);md_free(&d);}else assert(!d.text && !d.spans);
 }
 tests+=20000;printf("PASS: %zu verificações do núcleo.\n",tests);return 0;
}
