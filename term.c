#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <stdlib.h>
#include <shadow.h>
#include <crypt.h>
#include <semaphore.h>
#include <errno.h>


static struct termios old, new;

void term_setup(void)
{
#if defined(ARCH_ARM) || defined(ARCH_I386)
    tcgetattr(fileno(stdin), &old);
    new = old;
    new.c_cc[VERASE] = 0x7f; //0x08; dml!!!
    tcsetattr(fileno(stdin), TCSAFLUSH, &new);
#endif
}

void term_set_mode_ncan(void)
{
#if defined(ARCH_ARM) || defined(ARCH_I386)
    tcgetattr(fileno(stdin), &old);
    new = old;
    new.c_lflag &= ~ICANON;
    new.c_lflag &= ~ECHO;
    new.c_lflag &= ~ISIG;
    new.c_cc[VMIN] = 1;
    new.c_cc[VTIME] = 0;

    tcsetattr(fileno(stdin), TCSAFLUSH, &new);
#endif
}

void term_unset_mode_ncan(void)
{
#if defined(ARCH_ARM) || defined(ARCH_I386)
    tcsetattr(fileno(stdin), TCSAFLUSH, &old);
#endif
}

void term_set_mode_linux_console() {
#if defined(ARCH_ARM) || defined(ARCH_I386)
    tcgetattr(fileno(stdin), &old);
    new = old;
    new.c_lflag |= ICANON;
    new.c_lflag |= ECHO;
    new.c_lflag |= ISIG;

    new.c_cc[VINTR]    = 0;     /* Ctrl-c */
    new.c_cc[VQUIT]    = 0;     /* Ctrl-\ */
    new.c_cc[VERASE]   = 0;     /* del */
    new.c_cc[VKILL]    = 0;     /* @ */
    new.c_cc[VEOF]     = 4;     /* Ctrl-d */
    new.c_cc[VTIME]    = 0;     /* inter-character timer unused */
    new.c_cc[VMIN]     = 1;     /* blocking read until 1 character arrives */
    new.c_cc[VSWTC]    = 0;     /* '\0' */
    new.c_cc[VSTART]   = 0;     /* Ctrl-q */
    new.c_cc[VSTOP]    = 0;     /* Ctrl-s */
    new.c_cc[VSUSP]    = 0;     /* Ctrl-z */
    new.c_cc[VEOL]     = 0;     /* '\0' */
    new.c_cc[VREPRINT] = 0;     /* Ctrl-r */
    new.c_cc[VDISCARD] = 0;     /* Ctrl-u */
    new.c_cc[VWERASE]  = 0;     /* Ctrl-w */
    new.c_cc[VLNEXT]   = 0;     /* Ctrl-v */
    new.c_cc[VEOL2]    = 0;     /* '\0' */

    tcsetattr(fileno(stdin), TCSAFLUSH, &new);
#endif
}
