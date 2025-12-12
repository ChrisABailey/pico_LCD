#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"

const char BACKSPACE = 127;

unsigned char mygetchar() {
	int c;
	while ( (c = getchar_timeout_us(0)) < 0); 
	return (unsigned char)c;
};

int getString(char *line, int bufLen, bool echo ) {
  int cnt = 0;
  int currentChar;

  while (cnt < bufLen - 1) {
    currentChar = mygetchar();
    //printf("Debug char number: 0x%x",currentChar);
    if (currentChar == '\n' || currentChar == '\r' ||currentChar == ';' )
      break;
    if (currentChar == BACKSPACE) {
      if (echo) {
        putchar('\b');
        putchar(' ');
        putchar('\b');
      }
      cnt--;
      line[cnt] = ' ';
    } else { 
       // printf("Debug char number: %d",currentChar);
      if (echo)
        putchar(currentChar);
      line[cnt++] = currentChar;
    }
  }
  line[cnt++] = '\0';
  //printf("getString returned(%s)",line);
  return cnt;
}

// read characters from the serial port and convert them to a float after 8
// characters or when the user hits enter. Echos the characters as the user
// types. Handles backspace
float getFloat(bool echo ) {
  char line[9];
  getString(line, 9, echo);
  return atof(line);
}

int getInt(bool echo ) {
  char line[9];
  getString(line, 9, echo);
  return strtol(line,NULL,0);
}