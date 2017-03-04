/*
Copyright 2001-2003 David Gucwa

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include<stdio.h>
#include<stdlib.h>
#include<math.h>
#include<string.h>

#include "dav.h"
#include "efm_c.h"
#include "cport.h"

static void display_buffer();
static void moveUp();
static void moveDown();
static char moveLeft(); //Returns 1 when you hit the beginning of the buffer
static char moveRight(); //Returns 1 when you hit the end of the buffer
static void displayScreen();
static void setLineInfo(int line);
static void save();
static void load(char *filename);
static void scrollDown();
static void scrollUp();
static void doArguments(int argc, char *argv);
static int keyHit(int keypress, char undoNow);
static void checkSizes();
static void displayLine(int line);
static void quit(char *text);
static void displayHelp();
static void displayVersion();
static void askLoad();
static void showRow();
static void sigcatch() { quit(""); }
static void helpBar();
static void search();
static void Do(int keypress);
static void addToUndo(int keypress);
static void Undo();
static void loadSettings();

// ====================== terminal ===========================
static void move(int y, int x) {
	char buf[ 10 ];
	sprintf(buf, "\x1B[%d;%dH", y+1 , x+1 );
	wr_str(buf);
}

static void mvaddch(int y, int x, char c) {
	//move(y, x);
	//wr_char(c);
	char buf[ 10 ];
	sprintf(buf, "\x1B[%d;%dH%c", y+1 , x+1 ,c);
	wr_str(buf);
}

static void mvaddstr(int y, int x, char *s) {
	/*char *str;
	int len = strlen(s);
	str = (char *)efm_malloc(len + 10);
	memset(str, 0, len + 10);
	sprintf(str, "\x1B[%d;%dH%s", y , x ,s);
	wr_str(str);
	efm_free(str);*/
	move(y, x);
	wr_str(s);
}

static void getmaxyx(void *scr, int *mY, int *mX) {
	*mY = 23;
	*mX = 79;
}

static void mvgetstr(int y, int x, char *s) {
	int ch;
	move(y, x);
	while (1) {
		ch = rd_char();
		wr_char((char)ch);
		if (ch == '\r') return;
		else *s++ = (char)ch;
	}
}

static int g_state = 0;

int getch() {	
	int ch;
	
	while (1) {
		ch = rd_char();
		switch(g_state) {
		case 0:
			if (ch == 0x1B) {
				g_state = 1;
				continue;
			} else {
				if (ch == 0x7F) return 330;
				else if (ch == 0x08) return 8;
				else if (ch == 0x1A) return 270;
				else if (ch == 17) return 269;
				else return ch;
			}
			break;
		case 1:
			if (ch == 0x5B) {
				g_state = 2;
				continue;
			} else if (ch == 0x4F) {
				g_state = 3;
				continue;
			} else {
				g_state = 0;
				return ch;
			}
			break;
		case 2:
			g_state = 0;
			if (ch == 'A') {
				return 259;
			} else if (ch == 'B') {
				return 258;
			} else if (ch == 'D') {
				return 260;
			} else if (ch == 'C') {
				return 261;
			}
			
			break;
		case 3:
			g_state = 0;
			if (ch == 'P') {
				return 265; //F1				
			} else if (ch == 'Q') {
				return 266; //F2				
			} else if (ch == 'R') {
				return 267; //F3				
			} else if (ch == 'S') {
				return 268; //F4				
			}
			
			break;
		case 4:
			break;
		case 5:
			break;
		default: 
			break;
		}
	}
}
// ===================================================================


unsigned char *text_buffer;
int maxY,maxX;
int cursX=0,cursY=0;
int cursor;
int topleft=0; 
int lengthfBuffer;
int maxLength;
int *lineStart;
char *lineLength;
char searchString[80];
int numLines;
int maxLines;
int lineInfoUpdate;
int helpBarUpdate=0;
int topLine=0; //Which line is the top one on the screen
unsigned char lineUpdate=128; //Which line(s) need to be updated
//128 denotes updating every line after the selected one
//Low 7 bits denote the actual line selected

char *version = "0.3.0";
char fname[80]; //Remember filename
char updated = 0;
short *undoBuffer;
int maxUndoLength = 500;
char undoEnabled=1;

int dav(int argc, char *argv)
{
  int x,y;
  int keypress; 

  //Set up initial buffer
  maxLength=65536;
  text_buffer = (unsigned char *)efm_malloc((maxLength + 1)*sizeof(char));
  memset(text_buffer,0,maxLength);
  
  maxLines = 64;
  lineStart = (int *)efm_malloc((maxLines + 1)*sizeof(int));
  lineLength = (char *)efm_malloc((maxLines + 1)*sizeof(char));
  memset(lineStart,0,maxLines*sizeof(int));
  memset(lineLength,0,maxLines*sizeof(char));
  numLines = 1;  
  lineStart[0] = 0;
  lineLength[0] = 1;
  lengthfBuffer = 1;
  text_buffer[0] = 255;
  if(undoEnabled) undoBuffer = (short *)efm_malloc((maxUndoLength+1)*sizeof(short));
 
  cursor = 0;  
  topleft = 0;
   
  
  getmaxyx(NULL,&maxY,&maxX);
/*  
  for(x=0;x<maxX;x++)
    for(y=0;y<maxY;y++)
      mvaddch(y,x,' ');
*/      
  for(y = 0;y < maxY;y++) {
	  char *s = (char *)efm_malloc(maxX+1);
	  memset(s, ' ',maxX);
	  s[maxX] = 0;
	  mvaddstr(y,0,s);
	  efm_free(s);
  }
  
  move(0,0);
  cursY=0;
  cursX=0;

  if(argc) doArguments(argc,argv);

  setLineInfo(0);
  helpBar();
  lineInfoUpdate = 0;
  updated = 0;
  showRow();
  while(1)
  {
    keypress=getch();
    //if(undoEnabled) Do(keypress);
    if (keyHit(keypress,undoEnabled) == -1) {
    	return 0;
    }
    
    if(lineInfoUpdate) 
    {
      setLineInfo((lineUpdate&127) + topLine);
      lineInfoUpdate--;
    }
    displayScreen();
    showRow();
    checkSizes();    
    if(helpBarUpdate)
    {
      helpBarUpdate--;
      if(!helpBarUpdate) helpBar();
    }
  }
 
  return 0;
}

static void quit(char *text)
{
  if (lineStart) {
	  efm_free(lineStart);
	  lineStart = NULL;
  }
  if (lineLength) {
	  efm_free(lineLength);
	  lineLength = NULL;
  }
  if (text_buffer) {	  
	  efm_free(text_buffer);
	  text_buffer = NULL;
  }
  if(undoEnabled) {
	  if (undoBuffer) {
		  efm_free(undoBuffer);
		  undoBuffer = NULL;
	  }
  }
  //printf("%s",text);
}

static void displayLine(int line)
{
  unsigned char ch;
  int x=0;
  int t = lineStart[line + topLine];
  char count=0;
  char *cline = (char *)efm_malloc(maxX + 1);
  
  memset(cline, 0, maxX);
  if(line+topLine>numLines+1) 
  {
	  //memset(cline, ' ', maxX - 1);
	  
	  for(;x<maxX;x++)
		  cline[x] = ' ';
		  //mvaddch(line,x,' ');  
	  cline[x] = 0;
	  mvaddstr(line,0,cline);
	  return;
  }
  if(t==cursor) { cursX=x; cursY=line; }
  while(count<=lineLength[line+topLine] && t<lengthfBuffer && x < maxX)
  {
    count++;
    ch = text_buffer[t];
    if(t==cursor) { cursX=x; cursY=line; }
    t++;
    if(ch==9) //TAB
    {
      while(x%8!=7 && x < maxX)  
      {
        //mvaddch(line,x,' ');
    	cline[x] = ' ';  
        x++;
      }
    	
    }
    if(ch==255) ch=' ';
    cline[x] = ch;
    //mvaddch(line,x,ch);
    x++;
  }
  if (text_buffer[t - 1] == '\n') x--;
  for(;x<maxX;x++)
	  cline[x] = ' ';
    //mvaddch(line,x,' ');  
  cline[x] = 0;
  mvaddstr(line,0,cline);
  efm_free(cline);
}

static void displayScreen()
{
  int firstLine = lineUpdate & 127;
 
  displayLine(firstLine);
  if(lineUpdate & 128)
  {
    while(++firstLine<maxY-1)
      displayLine(firstLine);
  }
  lineUpdate = 0;
  move(cursY,cursX);
}

static void moveDown()
{
  if(cursY+topLine > numLines) 
  {
    cursor=lineStart[cursY+topLine]+lineLength[cursY+topLine]; 
    cursX=lineLength[cursY+topLine];
    return; 
  }
  if(cursY>=maxY-2) { scrollDown(); cursY--; lineUpdate=128; }
  else lineUpdate = cursY+1;
  if(cursX > lineLength[cursY+topLine+1]) cursX = lineLength[cursY+topLine+1];
  cursor = lineStart[cursY+topLine+1] + cursX;
  cursY++;
}

static void moveUp()
{
  if(cursY+topLine==0) { cursor=0; return; }
  if(cursY==0) { scrollUp(); cursY++; lineUpdate=128; }
  else lineUpdate = cursY-1;
  if(cursX > lineLength[cursY+topLine-1]) cursX = lineLength[cursY+topLine-1];
  cursor = lineStart[cursY+topLine-1] + cursX;
  cursY--;
}

static char moveLeft()
{
  if(!cursor) return 1;
  cursor--;
  if(text_buffer[cursor]=='\n' && cursY) cursY--;
  else if(cursor==lineStart[cursY+topLine]-1) cursY--;
  if(cursor==lineStart[topLine]-1)
  {
    scrollUp(); lineUpdate = 128;
  }
  else lineUpdate = cursY;
  return 0;
}

static char moveRight()
{
  if(++cursor>=lengthfBuffer) { cursor--; return 1; }
  if(cursor==lineStart[cursY+topLine+1]) { cursY++; cursX=0; }
  if(cursor==lineStart[maxY-1+topLine]) { scrollDown(); lineUpdate=128; }
  else lineUpdate = cursY;
  return 0;
}

static void setLineInfo(int line)
{
  int t = lineStart[line];
  int l = line+1;
  int numchars;
  while(1) 
  { 
    numchars=0;
    while(text_buffer[t] != '\n' && t<lengthfBuffer && numchars<maxX-1)
    {
      t++;
      numchars++;
    }
    t++;
    lineLength[l-1] = numchars;
    lineStart[l] = t;    
    l++;
    if(t>=lengthfBuffer) break;
  }
  lineStart[l] = t;
  lineLength[l-2]--;
  numLines = l-3;
}

static void save()
{
  int t;
  fs_file *fp;
  char file[80];
  mvaddstr(maxY-1,0,"File to save as?                                       ");
  if(!strcmp(fname,"")) mvgetstr(maxY-1,17,file);
  else strcpy(file,fname);
  fp = fs_fopen(file,"w");
  if(fp==NULL)
  {
    mvaddstr(maxY-1,0,
    "You do not have the proper permissions to save to that file");
    return;
  }

  fs_fwrite(text_buffer, 1, lengthfBuffer, fp );
  /*for(t=0;t<lengthfBuffer-1;t++)
    putc(text_buffer[t],fp);*/
  fs_fclose(fp);
  mvaddstr(maxY-1,0,"File successfully saved.                                                       ");
  helpBarUpdate=2;
  strcpy(fname,file);
  updated = 0;
}

static void scrollDown()
{
  //if(topLine==numLines-2) return;
  if(cursY==0) moveDown();
  lineUpdate = 128; 
  topLine++; 
}

static void scrollUp()
{
  if(!topLine) return;
  if(cursY>=maxY-2) moveUp();
  lineUpdate = 128;
  topLine--;
}

static void doArguments(int argc, char *argv)
{
  int x;
  for(x=0;x<argc;x++)
  {
    if(argv[0]!='-') //Must be trying to load a file then
    {
      load(argv);
    }
    if(!strcmp(argv,"--help")) displayHelp();
    if(!strcmp(argv,"--version")) displayVersion();
  }
}

static int keyHit(int keypress, char undoNow)
{
  int t;
  int x;
  static char ctrl=0;
  //if(keypress==27) { ctrl=3; return 0; }
  // \x4F (F1, F2 ... F12)
  if(keypress==79 && ctrl==3) { ctrl--; return 0; }
  // F6
  if(keypress==99 && ctrl==2) 
  {
    ctrl=0;
    t=cursY;
    if(undoNow) Do(8192);
    while(text_buffer[cursor]!=' ' && text_buffer[cursor]!='\n' 
    && text_buffer[cursor]!=9)
      if(moveRight()) break;
    while(text_buffer[cursor]==' ' || text_buffer[cursor]=='\n'
    || text_buffer[cursor]==9)
      if(moveRight()) break;
    if(t==cursY) lineUpdate=cursY;
    else lineUpdate=128;
    if(t!=cursY) if(undoNow) Do(258);
    return 0;
  }
  // F7
  if(keypress==100 && ctrl==2)
  {
    ctrl=0;
    t=cursY;
    if(undoNow) Do(8192);
    moveLeft();
    while(text_buffer[cursor]==' ' || text_buffer[cursor]=='\n'
    || text_buffer[cursor]==9)
      if(moveLeft()) break;
    while(text_buffer[cursor]!=' ' && text_buffer[cursor]!='\n'
    && text_buffer[cursor]!=9)                                 
      if(moveLeft()) break; 
    moveRight();
    if(t==cursY) lineUpdate=cursY;
    else lineUpdate=128;
    if(t!=cursY) if(undoNow) Do(259);
    return 0;
  }
  // .\5B (arrows)
  if(keypress==91 && ctrl==3) { ctrl--; return 0; }
  if(keypress==55 && ctrl==2) { ctrl=1; return 0; }
  
  else if(keypress==94 && ctrl==1)
  {  
    cursor=cursY=cursX=topLine=0;
    lineUpdate=128;
    ctrl=0;
    return 0;
  }
  else if(keypress==335)
  {
    cursor=lineStart[numLines];
    cursX=0;
    cursY=maxY-2;
    topLine=numLines-22;
    lineUpdate=128;
    return 0;
  }
  // ------------------------------------------------------------------------
  ctrl=0;
  switch(keypress)
  {
    case 410:
    case -1:  //Window Resizing
      getmaxyx(NULL,&maxY,&maxX);
      helpBar();
      lineUpdate=128;
      lineInfoUpdate=1;
      while(cursY>=maxY-1) moveUp();
      break;   

    case 270: //F6 CTRL+Z
      if(undoEnabled) Undo();
      break;

    case 265: //F1
      search();
      break;

    case 269: //CTRL+Q
    case 27:  //ESC
    	if(!updated) quit("");
        mvaddstr(maxY-1,0,"Save before quit? [Y]/n/c                                ");
        move(maxY-1,26);
        t = getch();
        mvaddch(maxY-1,26,' ');
        if(t=='c' || t=='C') return -1;
        if(t=='n' || t=='N') {
        	quit("");
        	return -1;
        }
        save();
        quit("");
    	return -1;
  
    case 266: //F2
      save();
      break;

    case 268: //F4
      askLoad();    
      break;
 
    case 267: //F3
      strcpy(fname,"");
      save();
      break;
  // DOWN
    case 258: 
      if(undoNow) Do(258);
      moveDown(); 
      return 0;
  // UP
    case 259:
      if(undoNow) Do(259);
      moveUp(); 
      return 0;
  // LEFT
    case 260:
      if(undoNow) Do(260);
      moveLeft();
      return 0;
    // RIGHT
    case 261:
      if(undoNow) Do(261);
      moveRight();
      return 0;

    case 338: //PgDn
      if(undoNow) Do(338);
      x = cursY + 2;
      for(t=0;t<maxY-1;t++)
        moveDown();
      for(t=0;t<maxY-x;t++)
        scrollDown();
      if(cursY+topLine>=numLines+1) 
      { 
        cursY=numLines-topLine+1; 
        cursX=0;
        cursor=lineStart[cursY+topLine];
      }
      break;

    case 339: //PgUp
      if(undoNow) Do(339);
      x = cursY;
      for(t=0;t<maxY-1;t++)
        moveUp();
      for(t=0;t<x;t++)
        scrollUp();
      break;

    case 262: //Home
      cursor = lineStart[cursY+topLine];
      lineUpdate = cursY;
      if(undoNow) Do(8192);
      cursX=0;
      break;

    case 360: //End
      cursor = lineStart[cursY+topLine] + lineLength[cursY+topLine];
      lineUpdate = cursY;
      if(undoNow) Do(8192);
      cursX = lineLength[cursY+topLine];
      break;

    case 263: //Backspace
    case 8: //Shift- or Ctrl-Backspace
      if(undoNow) Do(263);
      if(cursor==0) return 0;
      lineUpdate = cursY;
      if(cursor==lineStart[cursY+topLine] || lineLength[cursY+topLine]==maxX-1)
      { 
    	if(!cursY && topLine) scrollUp();
        lineUpdate--; 
        lineUpdate|=128; 
        numLines--;  
      }
      memmove(text_buffer+cursor-1,text_buffer+cursor,lengthfBuffer-cursor);
      cursor--;
      lengthfBuffer--;
      updated = 1;
      lineInfoUpdate = 1;
      break;

    case 330: //Delete
      if(undoNow) Do(330);
      lineUpdate = cursY;
      if(text_buffer[cursor]=='\n' || lineLength[cursY+topLine]==maxX-1) { lineUpdate |= 128; numLines--; }
      if(cursor==lengthfBuffer-1) return 0;
      memmove(text_buffer+cursor,text_buffer+cursor+1,lengthfBuffer-cursor);
      lengthfBuffer--;    
      updated = 1;
      lineInfoUpdate = 1;
      break;

    case 21: //Ctrl-U
      keyHit(262,undoNow);
      //cursor = lineStart[cursY+topLine];
      lineUpdate = cursY|128;
      cursX=0;
      for(t=lineLength[cursY+topLine]+1;t;t--)
      { 
        if(undoNow) Do(330);
        keyHit(330,0);
      }
      break;

    case 11: //Ctrl-K
      lineUpdate = cursY;
      if(cursX==lineLength[cursY+topLine]) 
      { lineUpdate|=128; keyHit(330,0); return 0; }
      for(t=lineLength[cursY+topLine]-cursX;t;t--)
      {
        if(undoNow) Do(330);
        keyHit(330,0);
      }
      if(undoNow) Do(16384); 
      break;
    default:
      if(undoNow) Do(keypress);
      lineUpdate=cursY;
      if(keypress==13) //Enter
      {
        if(cursY==maxY-2) scrollDown();
        cursY++;
        lineUpdate|=128;
        numLines++;
        keypress='\n';
        cursX=0;
      }
      memmove(text_buffer+cursor+1,text_buffer+cursor,lengthfBuffer-cursor);
      text_buffer[cursor]=keypress;
      cursor++;
      lengthfBuffer++;
      updated = 1;
      if(lineLength[cursY+topLine]==maxX-1) lineUpdate|=128;
      lineInfoUpdate = 1;
  }
  return 0;
}

static void load(char *filename)
{
  int i,j;
  int ch;
  fs_file *fp = fs_fopen(filename,"r");
  lengthfBuffer=1;
  text_buffer[0] = 255;
  cursor=0;
  cursY=0;
  cursX=0;
  topLine=0;
  if(fp==NULL) 
  {

  }
  else
  {
	  char *s = (char *)efm_malloc(1024 + 1);
	  while ((i = fs_fread(s, 1, 1024, fp)) > 0) {
		  for (j = 0;j < i;j++) {
			  if(s[j]=='\n') s[j]=13;
			  keyHit(s[j],0);
			  checkSizes();
		  }
	  }
	  efm_free(s);
/*	  
    i = (int)fs_fread(&ch, 1, 1, fp);
    while(i!=0)
    {
      keyHit(ch,0);
      checkSizes();
      i = (int)fs_fread(&ch, 1, 1, fp); 
      if(ch=='\n') ch=13;
    }
*/    
    fs_fclose(fp);
  }
  cursor=0;
  lineUpdate=128;
  topLine=0;
  setLineInfo(0);
  displayScreen();
  strcpy(fname,filename);
  updated = 0;
}

static void checkSizes()
{
  void *new_buffer;
  if(lengthfBuffer>(maxLength>>1))
  {
    maxLength<<=1;
    new_buffer = (void *)efm_malloc((maxLength + 1)*sizeof(char));
    memset(new_buffer, 0, maxLength*sizeof(char));
    memcpy((unsigned char *)new_buffer,text_buffer,lengthfBuffer);
    efm_free(text_buffer);
    text_buffer = (unsigned char *)new_buffer;
  }
  if(numLines > (maxLines>>1))
  {
    maxLines<<=1;
    new_buffer = (void *)efm_malloc((maxLines+1)*sizeof(int));
    memset(new_buffer, 0,maxLines*sizeof(int));
    memcpy((int *)new_buffer,lineStart,(maxLines >> 1)*sizeof(int));
    efm_free(lineStart);
    lineStart = (int *)new_buffer;
    new_buffer = (void *)efm_malloc((maxLines+1)*sizeof(char));
    memset(new_buffer, 0,maxLines*sizeof(char));
    memcpy((char *)new_buffer,lineLength,maxLines*sizeof(char));
    efm_free(lineLength);
    lineLength = (char *)new_buffer;    
  }
}

static void displayVersion()
{
  efm_free(lineStart);
  efm_free(lineLength);
  efm_free(text_buffer);
  if(undoEnabled) efm_free(undoBuffer);
  printf("Dav version %s\n",version);
  exit(0);
}

static void displayHelp()
{
  efm_free(lineStart);
  efm_free(lineLength);
  efm_free(text_buffer);
  if(undoEnabled) efm_free(undoBuffer);
  printf("Dav v%s, written by David Gucwa\n",version);
  printf("Usage: dav [arguments] [FILENAME]\n");
  printf("  where FILENAME, if specified, is the name of the file you wish to load.\n");
  printf("Arguments list:\n");
  printf("  --help : Display this help screen\n");
  printf("  --version: Display the version of Dav that you are running\n");
  printf("Basic commands:\n");
  printf("  F1 : Search\n");
  printf("  F2 : Save current file\n");
  printf("  F3 : Save current file, prompt for filename\n");
  printf("  F4 : Load file from within Dav\n");
  printf("  Ctrl+q or ESC : Quit (ask for save if needed)\n");
  printf("  Ctrl+z : Undo last keypress\n");
  printf("  2xEsc : Quit (won't ask for save)\n");
  printf("  Ctrl-C : Quit (won't ask for save)\n");
  printf("  Ctrl-K : Erase to end of line\n");
  printf("  Ctrl-U : Erase whole line\n");
  printf("Personal options:\n");
}

static void askLoad()
{
  char file[80];  
  mvaddstr(maxY-1,0,"Load which file? [Enter cancels]                                ");
  mvgetstr(maxY-1,33,file);
  if(strcmp(file,"")) load(file);
  helpBar();  
}

static void showRow()
{
  char buf[20];

  sprintf(buf,"%03d/%03d",cursY+topLine, numLines+1);
  mvaddstr(maxY-1,maxX - (strlen(buf) + 1),buf);
  
  move(cursY,cursX);
}

static void helpBar()
{
  mvaddstr(maxY-1,0,"F1 Search - F2 Save - F3 Save As - F4 Load - ESC Quit - Ctrl+z Undo            ");
  move(cursY,cursX);
}

static void search()
{
  int t;
  if(!helpBarUpdate) 
  {
    mvaddstr(maxY-1,0,"Search for what word?                                                          ");
    mvgetstr(maxY-1,22,searchString);
  }
  t = (int)strstr(text_buffer+cursor+1,searchString);
  if(t=='\0') 
  {
    t = (int)strstr(text_buffer,searchString);
    if(t=='\0') 
    { 
      mvaddstr(maxY-1,0,"String not found.                                                              ");
      return;
    }
  }
  helpBarUpdate = 2;
  t-=(int)text_buffer;
  if(t==0)
  {
    cursY=0; cursX=0;
    cursor = 0;
    topLine = 0;
    lineUpdate = 128;
    return;
  }
  cursor = lineStart[cursY+topLine];

  if(t>cursor)
  {
    while(cursor<=t) moveDown();
    moveUp();
    cursor = t;
    cursX = t - lineStart[cursY+topLine];
  }
  else if(t<cursor)
  {
    while(cursor>=t) moveUp();
    moveDown();
    cursor = t;
    cursX = t - lineStart[cursY+topLine];
  }
  lineUpdate = 128;
}

int undoBufferPointer=0;
int undoBufferLength=0;

static void Do(int keypress)
{
  switch(keypress)
  {
    case 262:
    case 360:
    case 21:  //keyHit will take care of this
    case 11:  //keyHit will take care of this 
    case 270: //Undo key
      return;
    case 258:
      addToUndo(259);
      break;
    case 259:
      addToUndo(258);
      break;
    case 260:
      addToUndo(261);
      break;
    case 261:
      addToUndo(260);
      break;
    case 338:
      addToUndo(339);
      break;
    case 339:
      addToUndo(338);
      break;
    case 263:
      if(cursor) { addToUndo((int)text_buffer[cursor-1]); } 
      break;
    case 330:
      if(cursor<lengthfBuffer) { addToUndo(260); addToUndo((int)text_buffer[cursor]); } 
      break;
    case 16384:
      addToUndo(360);
      break;
    case 8192:
      addToUndo(8192|cursX);
      break;
    default:
      addToUndo(263);
  }
}

static void addToUndo(int keypress)
{
  undoBuffer[undoBufferPointer] = (short)keypress;
  undoBufferPointer++;
  undoBufferPointer %= maxUndoLength;
  if(++undoBufferLength>maxUndoLength) undoBufferLength--;
}

static void Undo()
{
  int key;
  if(undoBufferLength==0) return;
  undoBufferLength--;
  if(--undoBufferPointer == -1) undoBufferPointer=maxUndoLength-1;
  key = (int)undoBuffer[undoBufferPointer];
  if(key & 8192) { cursX=key & 0xFFF; cursor=lineStart[topLine+cursY]+cursX; lineUpdate=cursY; return; }
  if(key=='\n') key=13;
  keyHit(key,0);
}

