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
#include "cport.h"
#include "efm_c.h"
/*
 * setup I/O
 */
static int setup_io() {
    int br;
#if defined(OS_LINUX)
    br = B9600;
#else
    br = 9600;
#endif
    return setup_port(5, br);
}

static int close_io() {
#if defined(OS_LINUX)
    term_set_mode_linux_console();
#endif
    close_port();
    return 0;
}

int main(int argc, char *argv[]) {
    if (setup_io())
        exit(0);
    //printf("Starting\n");
    run_efm();
    close_io();
    return(0);
}

