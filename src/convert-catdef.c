#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void trim(char *s) {
  // remove trailing blanks and newlines
  char *p = s + strlen(s) -1;
  while (p >= s && (*p == ' ' || *p == '\n')) {
    *p-- = 0;
  }
}

int main(int argc, char **argv) {
  
  char catcmd[8];
  char catdescr[128];
  char catset[128];
  char catread[128];
  char catresp[256];

  int notenum;
  char notes[16][128];

  char *line;
  size_t linecap;

  char *pos;

  linecap = 1024;
  line = malloc(linecap);

  while (getline(&line, &linecap, stdin) > 0) {

    if ((pos=strstr(line,"//"))  != NULL) {
      pos += 2; 
      while (*pos != ' ' && *pos != 0) { pos++; }
      while (*pos == ' ' && *pos != 0) { pos++; }
      trim(pos);
    }

    if (strstr(line,"//CATDEF")  != NULL) {
      strcpy(catcmd, pos);
      notenum=0;
      catdescr[0]=0;
      catset[0]=0;
      catread[0]=0;
      catresp[0]=0;
      continue;
    }
    if (strstr(line,"//DESCR")  != NULL) {
      strcpy(catdescr, pos);
    }
    if (strstr(line,"//SET")  != NULL) {
      strcpy(catset, pos);
    }
    if (strstr(line,"//READ")  != NULL) {
      strcpy(catread, pos);
    }
    if (strstr(line,"//RESP")  != NULL) {
      strcpy(catresp, pos);
    }
    if (strstr(line,"//NOTE")  != NULL) {
      strcpy(notes[notenum], pos);
      notenum++;
    }
    if (strstr(line,"//ENDDEF")  != NULL) {
      // ship out
      printf("\\begin{center}\n");
      printf("\\begin{tabular}{|p{2cm}|p{11cm}|}\n");
      printf("\\toprule\n");
      printf("$\\phantom{\\Big|}$\\textbf{\\large %s} & %s \\\\\\cline{1-2}\n", catcmd, catdescr);
      if (*catset) {
        printf("$\\phantom{\\Big|}${\\large Set} & {%s} \\\\\\hline\n", catset);
      }
      if (*catread) {
        printf("$\\phantom{\\Big|}${\\large Read} & {%s} \\\\\\hline\n", catread);
      }
      if (*catresp) {
        printf("$\\phantom{\\Big|}${\\large Response} & {%s} \\\\\\hline\n", catresp);
      }
      if (notenum > 0) {
        printf("$\\phantom{\\Big|}${\\large Notes} & \\multicolumn{1}{|p{11cm}|}{%s} \\\\\n", notes[0]);
      }
      for (int i=1; i<notenum; i++) {
        printf(" & \\multicolumn{1}{|p{11cm}|}{%s} \\\\\n", notes[i]);
      }
      printf("\\bottomrule\n");
      printf("\\end{tabular}\n");
      printf("\\end{center}\n");
      printf("\n");
    }
  }
  return 0;
}
