/*
Copyright 2008-2009 Yury Bobrov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <stdio.h>
#include <string.h>
#include "cport.h"

#if !defined(OS_LINUX)

#include <windows.h>

static HANDLE hPort;

int open_cport(int port) {
    char buf[10];
    sprintf(buf, "COM%d", port);
    hPort = CreateFile(buf, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, 0);

    if (hPort == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            //serial port does not exist. Inform user.
            fprintf(stderr, "COM%d not found!\n", port);
            return -1;
        }
        //some other error occurred. Inform user.
        fprintf(stderr, "Error occured during open COM%d!\n", port);
        return -1;
    }
    return 0;
}

int set_cport_br(int br) {
    DCB dcbSerialParams = {
        0
    };

    // dcbSerial.DCBlength=sizeof(dcbSerialParams);
    if (!GetCommState(hPort, &dcbSerialParams)) {
        //error getting state
        fprintf(stderr, "Can not get COM state!\n");
        return -1;
    }
    dcbSerialParams.BaudRate = br;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hPort, &dcbSerialParams)) {
        //error setting serial port state
        fprintf(stderr, "Can not set COM state!\n");
        return -1;
    }
    return 0;
}

int set_port_tm() {
    COMMTIMEOUTS timeouts = {
        0
    };

    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hPort, &timeouts)) {
        //error occureed. Inform user
        fprintf(stderr, "Can not set COM timeouts!\n");
        return -1;
    }
    return 0;
}

int close_port() {
    CloseHandle(hPort);
    return 0;
}


int setup_port(int port, int br) {
    if (open_cport(port))
        return -1;
    if (set_cport_br(br))
        return -1;
    if (set_port_tm())
        return -1;
    return 0;
}

int read_port(char *data, int cnt, int *readed) {
    DWORD dwBytesRead = 0;
    int ret;

    if (!ReadFile(hPort, data, cnt, &dwBytesRead, NULL)) {
        //error occurred. Report to user.
        fprintf(stderr, "Can not read COM port!\n");
        return -1;
    }
    *readed = (int) dwBytesRead;
    return 0;
}

int write_port(char *data, int cnt) {
    DWORD dwWritten = 0;
    if (!WriteFile(hPort, data, cnt, &dwWritten, NULL)) {
        fprintf(stderr, "Can not write to COM port!\n");
        return -1;
    }
    return 0;
}

int cgetchar() {
    char data;
    int readed = 0;

    while (!readed) {
        if (read_port(&data, 1, &readed)) {
            return -1;
        }
    }
    return data;
}

int cputchar(char c) {
    return write_port(&c, 1);
}

int cputs(char *str) {
    int len = strlen(str);
    return write_port(str, len);
}

extern int vsprintf(char *, const char *, char *);

int cprintf(const char *fmt, ...) {
    char buf[1024];
    int ret;
    va_list ap; 
    va_start(ap, fmt);
    ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    cputs(buf);

    return ret;
}
#else
#include "term.h"

static int    com_fd;
int open_cport(int port) {
#if !defined(ARCH_I386)    
    char buf[50];
    sprintf(buf, "/dev/ttyS%d", port);
    freopen(buf, "r", stdin);
    freopen(buf, "w", stdout);
    freopen(buf, "w", stderr);
   
    com_fd = open(buf, O_RDWR);                          // Opening serial port asynchronously

    if (com_fd == -1) {
        return 1;
    }
#endif   
    return 0;
}

int close_port() {
#if !defined(ARCH_I386)     
    return close(com_fd);
#else
    return 0;
#endif    
}

int set_cport_br(int br) {
#if !defined(ARCH_I386)     
    struct termios options;
    tcgetattr(com_fd, &options);            //dml: remove this?
    cfsetispeed(&options, br);       //dml: remove this?
    tcsetattr(com_fd,  TCSANOW, &options);  //dml: remove this?
#endif    
    return 0;
}

int setup_port(int port, int br) {
    
    if (open_cport(port))
        return -1;
    if (set_cport_br(br))
        return -1;

    term_setup();
    term_set_mode_ncan();
   
    return 0;
}

#if 1
# ifdef getchar
#  undef getchar
# endif
# define STDIN_BUF_SIZE  4096
static char stdio_buf[STDIN_BUF_SIZE];
static int buf_head = 0, buf_tail = 0, buf_len = 0;

int kbhit(int tm) {
    int ret = -1;
//    int c, 
    int count;
    struct timeval tv;
    static fd_set fds;
    char buf[512];

    tv.tv_sec = tm;
    tv.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0

    ret = select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    if (ret <= 0)
        return -1;

    if (FD_ISSET(STDIN_FILENO, &fds)) {
        int i, flags = 0;
        flags = fcntl(STDIN_FILENO, F_GETFL);
        fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
        count = fread(buf,1,512, stdin);
        fcntl(STDIN_FILENO, F_SETFL, flags);
        for (i = 0;i < count; i++) {
            if (buf_len < STDIN_BUF_SIZE) {
                stdio_buf[buf_tail++ % STDIN_BUF_SIZE] = buf[i];
                buf_len++;
            } else
                break;
        }
        return 1;
    }

    return -1;
}

int c_getchar() {
    int c;
    int tm = 60 * 5000;

    if (buf_len > 0) {
        c = stdio_buf[buf_head++ % STDIN_BUF_SIZE];
        buf_len--;
        return(c);
    }

    c = kbhit(tm);

    if ((c > 0) && (buf_len > 0)) {
        c = stdio_buf[buf_head++ % STDIN_BUF_SIZE];
        buf_len--;
        return(c);
    } else
        return -1;
}

int c_get1char() {
    int c;
    int tm = 10000;

    if (buf_len > 0) {
        c = stdio_buf[buf_head++ % STDIN_BUF_SIZE];
        buf_len--;
        return(c);
    }

    c = kbhit(tm);

    if ((c > 0) && (buf_len > 0)) {
        c = stdio_buf[buf_head++ % STDIN_BUF_SIZE];
        buf_len--;
        return(c);
    } else
        return -1;
}

#define getchar c_getchar

#endif

#if 1

#ifdef putchar
    #undef putchar
#endif

int putchar(int ch) {
    int r;
    r = fputc(ch, stdout);
    fflush(stdout);

    return r;
}

#endif

#if 1

#ifdef puts
#   undef puts
#endif

int puts(const char *str) {
    int len = strlen(str);
#ifdef UNIX_LINE_END
    char nl = 1;
#else
    char nl = 0;
#endif
    while (*str) {
        unsigned char add_byte;

        if (*str == '\n') {
            if (!nl) {
                add_byte = '\r';
                nl = 1;
            } else {
                add_byte = '\n';
                nl = 0;
                str++;
            }
        } else {
            add_byte = *str;
            str++;
        }
        putchar(add_byte);
    }
    fflush(stdout);
    return 0;
}
#endif

int read_port(char *data, int cnt, int *readed) {
     *readed = fread(data, 1, cnt, stdin);
     return 0;
}

int write_port(char *data, int cnt) {
    fwrite(data, 1, cnt, stdout);
    fflush(stdout);
    return 0;
}

int cgetchar() {
    char c;
    c = c_getchar();
    //printf("char is: %02X\n", c);
    return c;
/*
    char data;
    int readed = 0;

    while (!readed) {
        if (read_port(&data, 1, &readed)) {
            return -1;
        }
    }
    return data;
*/
}

int cputchar(char c) {
    int r;
    r = fputc(c, stdout);
    fflush(stdout);

    return r;
}

int cputs(char *str) {
    //return puts(str);
    int len = strlen(str);
    return write_port(str, len);
}

int cprintf(const char *fmt, ...) {
    char *buf;
    int ret;
    va_list ap;

    buf = (char *)malloc(1024);

    va_start(ap, fmt);
    ret = vsnprintf(buf, 1023, fmt, ap);
    va_end(ap);
    puts(buf);

    free(buf);

    return 0;
}

#endif
