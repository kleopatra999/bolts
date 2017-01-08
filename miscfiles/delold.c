/*Virus' delold - deletes users who haven't been on in X days. */
#include <stdio.h>

main(){
 FILE *fp;
 char temp[80], file[80], sys[80];
 int c=0;
 int days=30; /* change 30 to how ever many days you'd like. */
 sprintf(sys, "find -mtime +%d -name \"*.D\" -print > old",days);
 system(sys);
 fp=fopen("old", "r");
 fscanf(fp, "%s", temp);
 while(!feof(fp)){
  temp[strlen(temp)-2]='\0';
  printf("Deleting %s.\n", temp);
  sprintf(file, "rm %s.*", temp);
  system(file);
  c++;
  fscanf(fp, "%s", temp);

 }
 printf("%d users deleted.\n", c);
 return;
}
