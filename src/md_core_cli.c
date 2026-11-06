#include "md_document.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
 if(argc!=3 || strcmp(argv[1],"--text")){fprintf(stderr,"Uso de teste: mdv-core --text arquivo.md\n");return 2;}
 MdDocument d;char error[256];
 if(!md_load(argv[2],&d,error,sizeof(error))){fprintf(stderr,"%s\n",error);return 1;}
 size_t n=fwrite(d.text,1,d.length,stdout);int ok=n==d.length && fflush(stdout)==0;md_free(&d);return ok?0:1;
}
