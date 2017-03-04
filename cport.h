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
#ifndef CPORT_H
#define CPORT_H

#if defined(OS_LINUX)
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#endif

int open_cport(int port);
int set_cport_br(int br);
int set_port_tm();
int close_port();
int setup_port(int port, int br);

int write_port(char *data, int cnt);
int cgetchar();
int cputchar(char c);
int cputs(char *str);
int cprintf(const char *fmt, ...);

#endif

