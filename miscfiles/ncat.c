/**************************************************************************/
/* Ncat - version of Unix cat that displays text files with embedded nuts */
/* colour codes (eg, crypt syslog)                                        */
/**************************************************************************/

#include <stdio.h>

main(argc, argv)
int argc;
char *argv[];
{
if (argc<2) {
  printf("Usage: ncat filename\n");
  exit(1);
}

if (!ncat(argv[1])) {
  printf("ncat: file not found\n");
  exit(1);
}

exit(0);
}

/*** ncat function ***/
ncat(filename)
char *filename;
{
char text[256],*str;
FILE *fp;
int i;

char *colcode[]={
"\033[0m\0", "\033[1m\0", "\033[5m\0", "\033[7m\0", "\033[4m",
/* Foreground */
"\033[30m\0","\033[31m\0","\033[32m\0","\033[33m\0",
"\033[34m\0","\033[35m\0","\033[36m\0","\033[37m\0",
/* Background */
"\033[40m\0","\033[41m\0","\033[42m\0","\033[43m\0",
"\033[44m\0","\033[45m\0","\033[46m\0","\033[47m\0"
};

/* Codes used in a string to produce the colours when prepended with a '~' */
char *colcom[]={
"RS","OL","LI","RV","UL",
"FK","FR","FG","FY",
"FB","FM","FT","FW",
"BK","BR","BG","BY",
"BB","BM","BT","BW"
};

if (!(fp=fopen(filename,"r"))) {
  return 0;
}

/* Go through file */
while(!feof(fp) ) {
  text[0]='\0';
  fgets(text,254,fp);
  
  str=text;
  
  /* Process line from file */
  while(*str) {
    
    /* Reset colours before newline */
    if (*str=='\n') {
      printf("\033[0m\n"); 
      str++;
      continue;
    }
      
    /* Process colour commands in the file */
    if (*str=='~') {
      ++str;
      for(i=0;i<20;++i) {
	if (!strncmp(str,colcom[i],2)) {
	  printf("%s",colcode[i]);
	  str++; 
	  str++;
	  str++;
	  continue;
	}
      }
      --str;
    }
    
    printf("%c",*str);
    str++;
  }
}

fclose(fp);
return 1;
}
