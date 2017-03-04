/*
Copyright 2008-2016 Yuri Bobrov

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
#include "efm_c.h"
#include "dav.h"

#if defined(CONFIG_EMBEDDED) || defined (OS_LINUX)
#include "cport.h"
#endif

#define T_HORIZONTAL    0
#define T_VERTICAL      1

#define T_NORMAL        0
#define T_INVERSE       1

#define T_CURSOR_OFF    0
#define T_CURSOR_ON     1

#ifndef CONFIG_EMBEDDED
#include <dirent.h>
#include <sys/stat.h>
#include <wchar.h>
#endif

#ifdef NET_IO
extern struct netconn *get_conn(); 
extern int telnet_get_char(int tm);
#endif

int init_window(window_t *pWindow, unsigned int height, unsigned int width,
                unsigned int ncol, unsigned int nrow, unsigned int x,
                unsigned int y, unsigned int npanel,
                int (*handler) (void *arg), char *text, window_t *parent);
int edit_en_callback(void *parg);
int en_callback(void *parg);
void init_graphic(int type);
static int win_show_dir(window_t *win);
int win_read_dir_context(window_t *win, char *dir);
int fs_item_is_dir(fs_dirent *direntp, char *path);

//********************* Terminal *****************************************
static void vt_set_cursor_pos(int line, int col);
static void vt_clear_screen();
static void vt_clear_line(unsigned int mode);
static void vt_set_mode(unsigned int mode);

//****************** console dependent functions *************************
#ifdef NET_IO
void net_wr_str(const char *str) {
    struct netconn *conn = get_conn();
    netconn_write(conn, (void *) str, strlen(str), NETCONN_COPY);
}

void net_wr_data(char *data, int len) {
    struct netconn *conn = get_conn();
    netconn_write(conn, (void *) data, len, NETCONN_COPY);
}

void net_wr_char(char c) {
    struct netconn *conn = get_conn();
    netconn_write(conn, (void *) &c, 1, NETCONN_COPY);
}

int net_get_char() {
    return telnet_get_char(0);
}
#else
#if defined(PC_IO) || defined(OS_LINUX)
void com_wr_str(const char *str) {
    cputs((char *) str);
}

void com_wr_data(char *data, int len) {
    write_port(data, len);
}

void com_wr_char(char c) {
    cputchar(c);
}

int com_get_char() {
    return cgetchar();
}
#endif
#endif

//********************* COMMON functions *********************************

/*
 * make last entrie
 */
void last_entrie(char *p, int len) {
    unsigned int i;
    unsigned int cur_bs_pos = 0;

    for (i = 0; i < len; i++) {
        if (p[i] == EFM_DIR_SLASH_C) {
            cur_bs_pos = i;
        }
    }
#if defined(CONFIG_EMBEDDED) || defined (OS_LINUX)
    if (cur_bs_pos == 0) {
        p[0] = EFM_DIR_SLASH_C;
        p[1] = 0;
        return;
    }
#else
    if (cur_bs_pos <= 3) {
        strcpy(&p[0], "C:"EFM_DIR_SLASH_S);
        return;
    }
#endif
    p[cur_bs_pos] = 0;
}

/*
 * find last entrie pos
 */
int find_last_entrie(char *p) {
    unsigned int len = 0;

    len = strlen(p);
#if defined(CONFIG_EMBEDDED) || defined (OS_LINUX)
    if (len == 1)
        len = 0;
#else
    if (len == 3)
        len = 2;
#endif
    return len;
}

/*
 * set ROOT directory to the path
 */
int set_root(char *p, int len) {
    unsigned int i;
    unsigned int cur_bs_pos = 0;

    for (i = 0; i < len; i++) {
        if (p[i] == EFM_DIR_SLASH_C) {
            cur_bs_pos = i + 1;
            break;
        }
    }
    if (i == 0) {
        p[0] = EFM_DIR_SLASH_C;
        p[1] = 0;
        return 0;
    }

    p[cur_bs_pos] = 0;
}

/*
 * add entrie
 */
int add_entrie(char *path, char *name, unsigned int mode) {
    unsigned int entrie, i;

    if (mode) {
        for (i = 0; i < strlen(name); i++) {
            if (name[i] == ' ')
                name[i] = 0;
        }
    }
    entrie = find_last_entrie(path);
    path[entrie] = EFM_DIR_SLASH_C;
    strcpy(&path[entrie + 1], name);
    return 0;
}

/*
 * vt100 set cursor
 */
static void vt_set_cursor_pos(int line, int col) {
    char buf[10];
    sprintf(buf, "\x1B[%d;%dH", line, col);
    wr_str(buf);
}

/*
 * low level set position in window
 */
static void ll_set_pos(window_t *win, unsigned int x, unsigned int y) {
    win->cur_pos_x = x;
    win->cur_pos_y = y;
    set_cursor_pos(y, x);
}

/*
 * low level get current position of cursor in window
 */
static int ll_get_pos(window_t *win, unsigned int *x, unsigned int *y) {
    if (!win)
        return -1;
    *x = win->cur_pos_x;
    *y = win->cur_pos_y;
    return 0;
}

/*
 * low level write to terminal screen
 */
static void ll_write(char *screen, unsigned int start, unsigned int len) {
    wr_data(&screen[start], len);
}

/*
 * vt100 clear screen
 */
static void vt_clear_screen() {
    wr_str("\x1B[2J");
}

/*
 * low level clear screen
 */
static void ll_clear_screen() {
    clear_screen();
}

/*
 * vt100 clear line
 */
static void vt_clear_line(unsigned int mode) {
    wr_str("\x1B[2K");
}

/*
 * low level clear line
 */
static void ll_clear_line(unsigned int mode) {
    clear_line(mode);
}

/*
 * write text to the window:
 * win - window
 * data - text to write
 * row - start row
 * col - start column
 * len - text length
 * mode - horizontal or vertical text
 */
static int win_write(window_t *win, char *data, unsigned int *row,
                     unsigned int *col, unsigned int len, unsigned int mode) {
    unsigned int offset, nrow = (*row), ncol = (*col);
    if (!win)
        return -1;

    offset = ((win->width * (*row - win->s_y)) + (*col - win->s_x));
    if (mode == 0)
        ncol = win->cur_pos_x + (len % win->width);
    else
        nrow = win->cur_pos_y + (len % win->height);

    memcpy(&win->screen[offset], data, len);
    ll_write(win->screen, offset, len);
    ll_set_pos(win, ncol, nrow);
    return 0;
}

static int win_write_dry(window_t *win, char *data, unsigned int *row,
                     unsigned int *col, unsigned int len, unsigned int mode) {
    unsigned int offset, nrow = (*row), ncol = (*col);
    if (!win)
        return -1;

    offset = ((win->width * (*row - win->s_y)) + (*col - win->s_x));
    if (mode == 0)
        ncol = win->cur_pos_x + (len % win->width);
    else
        nrow = win->cur_pos_y + (len % win->height);

    memcpy(&win->screen[offset], data, len);
    // set current position intead writing on screen
    win->cur_pos_x = ncol;
    win->cur_pos_y = nrow;
    return 0;
}

/*
 * read text from window:
 * win - window
 * data - where to read text
 * row - start row
 * col - start column
 * len - text len (not used now)
 */
static int win_read(window_t *win, char *data, unsigned int *row,
                    unsigned int *col, unsigned int len) {
    unsigned int offset;
    offset = ((win->width * (*row - win->s_y)) + (*col - win->s_x));
    //memcpy(data,  &win->screen[offset],len);
    strcpy(data, &win->screen[offset]);
    return 0;
}

/*
 * clear terminal screen and set cursor to 1:1
 */
int win_clear_screen(window_t *win) {
    if (!win)
        return -1;
    ll_clear_screen();
    ll_set_pos(win, 1, 1);

    return 0;
}

/*
 * clear line in window
 * win - window
 * mode - vertical or horizontal line (not implemented)
 */
int win_clear_cur_line(window_t *win, unsigned int mode) {
    unsigned int x, y, offset;

    if (!win)
        return -1;

    ll_get_pos(win, &x, &y);
    x = 1;
    offset = (((win->width * y) - win->s_y) + (x - win->s_x));
    memset(&win->screen[offset], 0, win->width);
    ll_clear_line(mode);
    ll_set_pos(win, x, y);

    return 0;
}

/*
 * pd_u - panel delimiter upper;
 * pd_l - panel delimiter lower;
 * cd_u - column delimiter upper;
 * cd_l - column delimiter lower;
 * llh  - line horizontal;
 * llv  - line vertical;
 * llvp - panel line vertical;
 * cel  - colunm element;
 * ruc  - right upper corner;
 * luc  - left upper corner;
 * llc  - left lower corner;
 * rlc  - right lower corner;
 */
static char pd_u, cd_u, cd_l, pd_l, llh, llv, ruc, luc, llc, rlc, llvp, cel;

void init_graphic(int type) {
    if (type == 0) {
        pd_u = cd_u = cd_l = pd_l = '+';
        llh = '='; llv = '|'; llvp = 'I', cel = '|';
        ruc = luc = llc = rlc = '+'; 
    } else {
        llh = 205; llv = 186, llvp = 186, cel = 179;
        ruc = 201; luc = 187; llc = 188, rlc = 200;
        pd_u = 203, cd_u = 209, pd_l = 202, cd_l = 207;
    }
}


/*
 * draw line
 * win - window
 * line_element - charachter element for lin drowing ('*')
 * lelen - line elements length (for additional use)
 * mode - vertical or horizontal line
 * x - start x position
 * y - start y position
 * len - line length in elements
 */
int draw_line(window_t *win, char *line_element, unsigned int lelen,
              unsigned int mode, unsigned int x, unsigned int y,
              unsigned int len) {
    unsigned int i = 0, j;

    ll_set_pos(win, x, y);

    if (mode == T_HORIZONTAL) {
        // hor
        if (len > (win->width))
            len = win->width;
        for (j = 0; j < len; j++) {
            if (lelen > 1) {
                for (i = 0; i < lelen - 1; i++)
                    wr_char(line_element[i]);
            } else
                win_write(win, &line_element[i], &win->cur_pos_y,
                          &win->cur_pos_x, 1, T_HORIZONTAL);
        }
    } else if (mode == T_VERTICAL) {
        // vert
        if (len > (win->height))
            len = win->height;
        for (j = 0; j < len; j++) {
            if (lelen > 1) {
                for (i = 0; i < lelen - 1; i++)
                    wr_char(line_element[i]);
            } else
                win_write(win, &line_element[i], &win->cur_pos_y,
                          &win->cur_pos_x, 1, T_VERTICAL);
        }
    }
    return 0;
}

/*
 * draw rectangle
 * win - window
 * x - start x position
 * y - start y position
 * width - rec width
 * height - rec heigth
 */
int draw_rectangle(window_t *win, unsigned int x, unsigned int y,
                   unsigned int width, unsigned int height) {
    unsigned int tx, ty;

    tx = x;
    ty = y;
    // left top corner
    draw_line(win, &ruc, 1, T_HORIZONTAL, tx, ty, 1);
    // top horizontal line
    draw_line(win, &llh, 1, T_HORIZONTAL, tx + 1, ty, width - 1);
    tx += width - 1;
    // right top corner
    draw_line(win, &luc, 1, T_HORIZONTAL, tx, ty, 1);
    // right vertical line
    draw_line(win, &llv, 1, T_VERTICAL, tx, ty + 1, height - 1);
    ty += height - 1;
    tx -= width - 1;
    // buttom horizontal line
    draw_line(win, &llh, 1, T_HORIZONTAL, tx, ty, width - 1);
    tx += width - 1;
    // right buttom corner
    draw_line(win, &llc, 1, T_HORIZONTAL, tx, ty, 1);
    tx -= width - 1;
    ty -= height - 1;
    // left vertical line
    draw_line(win, &llv, 1, T_VERTICAL, tx, ty + 1, height - 1);
    ty += height - 1;
    // left buttom corner
    draw_line(win, &rlc, 1, T_HORIZONTAL, tx, ty, 1);

    return 0;
}

/*
 * set VT cursor ON/OFF
 */
static void vt_cursor_mode(unsigned int mode) {
    if (mode == T_CURSOR_ON)
        wr_str("\x1B[?25h");
    else if (mode == T_CURSOR_OFF)
        wr_str("\x1B[?25l");

}

/*
 * set VT normal or inverse mode
 */
static void vt_set_mode(unsigned int mode) {
    if (mode == T_NORMAL)
        wr_str("\x1B[0m");
    else if (mode == T_INVERSE)
        wr_str("\x1B[7m");
        //wr_str("\x1B#7\x1B[7m");
}

/*
 * draw text
 * win - window
 * x - start x position
 * y - start y position
 * text - text to write
 * len - text length
 * mode - horizontal or vertical text
 */
int draw_text(window_t *win, unsigned int x, unsigned int y, char *text,
              unsigned int len, unsigned int mode) {
    char *p = text;
    char *pc = NULL;
    int cur_y = y;
    int cur_len = 0;
    int ret = 0;

    set_mode(mode);

    while (!ret) {
        pc = strchr(p, '\n');

        if (pc) {
            cur_len = pc - p;
        } else {
            cur_len = len;
            ret = 1;
        }
        ll_set_pos(win, x, cur_y);
        win_write(win, p, &win->cur_pos_y, &win->cur_pos_x, cur_len,
                  T_HORIZONTAL);
        len = len - (cur_len + 1);
        if (len) {
            p = pc + 1;
            if (++cur_y >= win->num_row) {
                break;
            }
        } else
            break;
    }
    // reverse mode
    switch (mode) {
    case T_NORMAL:
        // normal mode
        // stand int this mode
        break;
    case T_INVERSE:
        // inverse mode
        // return in normal mode
        set_mode(T_NORMAL);
        break;
    }
    return 0;
}

/*
 * draw text to the screen buffer only (need to be printed)
 */
int draw_text_dry(window_t *win, unsigned int x, unsigned int y, char *text,
              unsigned int len, unsigned int mode) {
    char *p = text;
    char *pc = NULL;
    int cur_y = y;
    int cur_len = 0;
    int ret = 0;

    while (!ret) {
        pc = strchr(p, '\n');

        if (pc) {
            cur_len = pc - p;
        } else {
            cur_len = len;
            ret = 1;
        }
        win->cur_pos_x = x;
        win->cur_pos_y = cur_y;
        win_write_dry(win, p, &win->cur_pos_y, &win->cur_pos_x, cur_len,
                  T_HORIZONTAL);
        len = len - (cur_len + 1);
        if (len) {
            p = pc + 1;
            if (++cur_y >= win->num_row) {
                break;
            }
        } else
            break;
    }
    return 0;
}

/*
 * alloc window structure
 */
window_t * new_window() {
    return efm_malloc(sizeof(window_t));
}

#if 0
/*
 * create subwindow in the window:
 * win - subwindow structure
 * height - height
 * width - width
 * nrow - rows in symbols
 * ncol - columns in symbols
 * x - start x position
 * y - start y position
 */
int win_create_sub_window(window_t *win, unsigned int height,
                          unsigned int width, unsigned int nrow,
                          unsigned int ncol, unsigned int x, unsigned int y) {
    win->screen = (char *) efm_malloc(height * width);
    if (!win->screen)
        return -1;

    win->num_col = ncol;
    win->num_row = nrow;
    win->s_x = x;
    win->s_y = y;

    memset(win->screen, 0, height * width);
    return 0;
}
#endif
/*
 * create new EDIT object:
 * caption - caption text
 * callback - edit callback function
 * len - edit length
 * active - activ or not in window
 */
edit_t * new_edit(char *caption, win_callback_t callback, unsigned int len,
                  unsigned int active) {
    edit_t *edit = (edit_t *) efm_malloc(sizeof(edit_t));
    if (!edit)
        return NULL;
    edit->buf = (char *) efm_malloc(len);
    edit->len = len;
    edit->edit_func = callback;
    edit->id = EDIT;
    return edit;
}

/*
 * convert EDIT to element structure
 */
element_t * edit_2_el(edit_t *edit) {
    element_t *el = (element_t *) efm_malloc(sizeof(element_t));
    if (!el)
        return NULL;
    el->element_type = EDIT;
    el->pelement = (void *) edit;
    return el;
}

/*
 * draw text in the EDIT
 */
int edit_add_text(window_t *win, edit_t *edit, char *text, unsigned int len) {
    if (len >= edit->len)
        return -1;

    if (edit->cur_pos_x >= (edit->x + edit->w - 1))
        return -1;
    draw_text(win, edit->cur_pos_x, edit->cur_pos_y, text, len, T_NORMAL);
    edit->cur_pos_x++;
    return 0;
}

/*
 * delete text in the EDIT
 */
int edit_del_text(window_t *win, edit_t *edit, unsigned int len) {
    char *buf = (char *) efm_malloc(128);
    unsigned int offset;

    if (!buf)
        return -1;

    memset(buf, 0, 128);

    if (edit->cur_pos_x > edit->x + 1) {
        edit->cur_pos_x--;
        offset = ((win->width * (edit->cur_pos_y - win->s_y)) +
                  (edit->cur_pos_x - win->s_x));
        win->screen[offset] = 0;
        ll_set_pos(win, edit->x + 1, edit->cur_pos_y);
        win_read(win, buf, &win->cur_pos_y, &win->cur_pos_x, edit->len);
        // clear char on screen
        ll_set_pos(win, edit->cur_pos_x, edit->cur_pos_y);
        wr_char(' ');
        draw_text(win, edit->x + 1, edit->cur_pos_y, buf, strlen(buf),
                  T_NORMAL);
    } else {
        edit->cur_pos_x = edit->x + 1;
    }

    efm_free(buf);

    return 0;
}

/*
 * get text from the EDIT
 */
int edit_get_text(window_t *win, edit_t *edit, void *text) {
    ll_set_pos(win, edit->x + 1, edit->cur_pos_y);
    win_read(win, (char *) text, &win->cur_pos_y, &win->cur_pos_x, edit->len);
    return 0;
}

/*
 * set EDIT selection
 */
int win_set_edit_sel(window_t *win, edit_t *e, unsigned int mode) {
    ll_set_pos(win, e->cur_pos_x, e->cur_pos_y);

    return 0;
}

/*
 * return first EDIT element if exists
 */
edit_t * win_get_edit(window_t *win) {
    unsigned int i;
    element_t *el;

    for (i = 0; i < win->nelement; i++) {
        el = (element_t *) win->element[i];
        if (el->element_type == EDIT) {
            edit_t *e;
            e = (edit_t *) el->pelement;
            return e;
        }
    }
    return NULL;
}

/*
 * create new BUTTON structure:
 * caption - caption text
 * callback - callback function
 * cx - start x position
 * cy - start y position
 * active - active button or not in window
 */
button_t * new_button(char *caption, win_callback_t callback, unsigned int cx,
                      unsigned int cy, unsigned int active) {
    button_t *but = (button_t *) efm_malloc(sizeof(button_t));
    if (!but)
        return NULL;
    but->caption = caption;
    but->button_func = callback;
    but->id = BUTTON;
    but->c_x = cx;
    but->c_y = cy;
    return but;
}

/*
 * convert BUTTON to element structure
 */
element_t * but_2_el(button_t *but) {
    element_t *el = (element_t *) efm_malloc(sizeof(element_t));
    if (!el)
        return NULL;
    el->element_type = BUTTON;
    el->pelement = (void *) but;
    return el;
}

/*
 * reurn active BUTTON in the window if exists, or NULL
 */
button_t * get_ative_but(window_t *win) {
    unsigned int i;
    element_t *el;
    button_t *b;

    for (i = 0; i < win->nelement; i++) {
        el = win->element[i];
        if (el->element_type == BUTTON) {
            b = (button_t *) el->pelement;
            if (b->active)
                return b;
        }
    }
    return NULL;
}

/*
 * set button selection
 */
int win_set_but_sel(window_t *win, button_t *b, unsigned int mode) {
    char buf[20];
    unsigned int len;

    memset(buf, 0, 20);

    b->active = mode;
    ll_set_pos(win, b->c_x, b->c_y);
    win_read(win, buf, &b->c_y, &b->c_x, 1);
    len = strlen(buf);
    if (len) {
        if (mode)
            draw_text(win, b->c_x, b->c_y, buf, len, T_INVERSE);
        else
            draw_text(win, b->c_x, b->c_y, buf, len, T_NORMAL);
    }
    return 0;
}
/*
 * set selected nex button
 */
int win_next_but(window_t *win) {
    button_t *but = get_ative_but(win);
    button_t *b = but->next;

    if (!but)
        return 0;
    if (b) {
        but->active = 0;
        b->active = 1;
        win_set_but_sel(win, but, T_NORMAL);
        win_set_but_sel(win, b, T_INVERSE);
    } else {
        but->active = 1;
        win_set_but_sel(win, but, T_INVERSE);
    }
    return 0;
}

/*
 * place element
 * win - window
 * el - element type
 * x - start x position
 * y - start y position
 * w - width of element
 * h - height of element
 */
int win_place_element(window_t *win, element_t *el, unsigned int x,
                      unsigned int y, unsigned int w, unsigned int h) {
    element_t *pel;

    el->next = el;

    if (win->nelement > 0) {
        pel = (element_t *) win->element[win->nelement - 1];
        pel->next = el;
        el->next = win->element[0];
    }

    win->element[win->nelement++] = (void *) el;

    if (el->element_type == BUTTON) {
        button_t *b = (button_t *) el->pelement;
        b->x = x;
        b->y = y;
        b->w = w;
        b->h = h;
        b->c_x = x + 1 + (((w - 1) - strlen(b->caption)) / 2);
        b->c_y = y + 1;
    } else if (el->element_type == EDIT) {
        edit_t *e = (edit_t *) el->pelement;
        e->x = x;
        e->y = y;
        e->w = w;
        e->h = h;
        e->cur_pos_x = x + 1;
        e->cur_pos_y = y + 1;
    }
    return 0;
}

/*
 * return active element 
 */
int win_get_active_element(window_t *win) {
    unsigned int i;
    element_t *el;

    for (i = 0; i < win->nelement; i++) {
        el = (element_t *) win->element[i];
        if (el->active)
            return i;
    }
    return -1;
}

/*
 * set active element 
 * nel - element index
 */
int win_set_active_element(window_t *win, unsigned int nel) {
    element_t *el;
    int nnel = -1;

    if (nel < win->nelement) {
        nnel = win_get_active_element(win);
        if (nnel >= 0) {
            el = (element_t *) win->element[nnel];
            el->active = 0;
        }
        el = (element_t *) win->element[nel];
        el->active = 1;
    }
    return 0;
}

/*
 * set next elemet  selected
 */
void win_next_element(window_t *win) {
    element_t *el;
    int nel = -1;

    nel = win_get_active_element(win);
    if (nel < 0)
        return;
    el = (element_t *) win->element[nel];
    if (el->element_type == BUTTON) {
        button_t *b = (button_t *) el->pelement;
        b->active = 0;
        win_set_but_sel(win, b, T_NORMAL);
    }
    el->active = 0;
    el->next->active = 1;
    el = el->next;
    if (el->element_type == BUTTON) {
        button_t *b = (button_t *) el->pelement;
        b->active = 1;
        win_set_but_sel(win, b, T_INVERSE);
        //win_next_but(win);
    } else if (el->element_type == EDIT) {
        edit_t *e = (edit_t *) el->pelement;
        win_set_edit_sel(win, e, 0);
    }
}

/*
 * set element of index nel selected 
 */
void win_set_element_sel(window_t *win, unsigned int nel) {
    unsigned int nnel = -1;
    element_t *el;

    nnel = win_get_active_element(win);
    if (nnel >= 0) {
        el = (element_t *) win->element[nnel];
        el->active = 0;
        if (el->element_type == BUTTON) {
            button_t *b = (button_t *) el->pelement;
            win_set_but_sel(win, b, T_NORMAL);
        }
    }

    el = (element_t *) win->element[nel];
    el->active = 1;
    if (el->element_type == BUTTON) {
        button_t *b = (button_t *) el->pelement;
        win_set_but_sel(win, b, T_INVERSE);
    } else if (el->element_type == EDIT) {
        edit_t *e = (edit_t *) el->pelement;
        win_set_edit_sel(win, e, 0);
    }
}

/*
 * create sub window with edit for 50 symbols and "YES/NO" buttons
 * in the middle of the screen
 */
int win_create_edit_en_win(window_t *win, char *text, window_t *parent) {
    button_t *but_y, *but_n;
    edit_t *edit;
    element_t *el1, *el2, *el3;

    init_window(win, 9, 40, 1, 9, 30, 7, 1, edit_en_callback, text, parent);
    /*
     * caption text
     * callback function
     * start x position
     * start y position
     * active button or not 
    */
    but_y = new_button("YES", NULL, 0, 0, 1);
    but_n = new_button("NO", NULL, 0, 0, 0);
    but_y->next = but_n;
    but_n->next = but_y;
    el1 = but_2_el(but_y);
    el2 = but_2_el(but_n);
    edit = new_edit(NULL, NULL, 50, 0);
    el3 = edit_2_el(edit);
    win_place_element(win, el1, win->s_x + 2, win->s_y + 5, 10, 3);
    win_place_element(win, el2, (win->s_x + (win->width - 2 - 10)),
                      win->s_y + 5, 10, 3);
    win_place_element(win, el3, win->s_x + 2, win->s_y + 2, win->width - 4, 3);
    win_set_active_element(win, 2);
    return 0;
}

/*
 * create subwindow with "OK" button
 */
int win_create_help_win(window_t *win, char *text, window_t *parent) {
    button_t *but_ok;
    element_t *el1;

    init_window(win, 15, 60, 1, 15, 2, 2, 1, en_callback, text, parent);

    but_ok = new_button("OK", NULL, 0, 0, 1);

    but_ok->next = but_ok;
    el1 = but_2_el(but_ok);
    win_place_element(win, el1, win->s_x + 25, win->s_y + 10, 10, 3);

    win_set_active_element(win, 0);
    return 0;
}

/*
 * create subwindow with "OK" button
 */
int win_create_ok_win(window_t *win, char *text, window_t *parent) {
    button_t *but_ok;
    element_t *el1;

    init_window(win, 7, 50, 1, 7, 30, 9, 1, en_callback, text, parent);

    but_ok = new_button("OK", NULL, 0, 0, 1);

    but_ok->next = but_ok;
    el1 = but_2_el(but_ok);
    win_place_element(win, el1, win->s_x + 22, win->s_y + 3, 10, 3);

    win_set_active_element(win, 0);
    return 0;
}

/*
 * create subwindow with "YES/NO" buttons
 */
int win_create_en_win(window_t *win, char *text, window_t *parent) {
    button_t *but_y, *but_n;
    element_t *el1, *el2;

    init_window(win, 7, 40, 1, 7, 30, 9, 1, en_callback, text, parent);

    but_y = new_button("YES", NULL, 0, 0, 1);
    but_n = new_button("NO", NULL, 0, 0, 0);
    but_y->next = but_n;
    but_n->next = but_y;
    el1 = but_2_el(but_y);
    el2 = but_2_el(but_n);
    win_place_element(win, el1, win->s_x + 2, win->s_y + 3, 10, 3);
    win_place_element(win, el2, (win->s_x + (win->width - 2 - 10)),
                      win->s_y + 3, 10, 3);
    win_set_active_element(win, 0);
    return 0;
}

/*
 * free window structure
 */
int win_free_win(window_t *win) {
    efm_free(win->screen);
    if (win->backup_region) {
        efm_free(win->backup_region);
    }
    return 0;
}

/*
 * free window with all elements on it
 */
int win_remove_en_win(window_t *win) {
    unsigned int i;
    element_t *el;

    for (i = 0; i < win->nelement; i++) {
        el = win->element[i];
        if (el->element_type == BUTTON) {
            button_t *b;
            b = (button_t *) el->pelement;
            efm_free(b);
        } else if (el->element_type == EDIT) {
            edit_t *e;
            e = (edit_t *) el->pelement;
            if (e->buf)
                efm_free(e->buf);
            efm_free(e);
        }
        efm_free(el);
    }
    win_free_win(win);
    return 0;
}
/*
 * pWindow - window pointer
 * height - window height in symbols
 * width - window width in symbols
 * ncol - colomns per window
 * nrow - rows per window
 * x - start x position (in symbols)
 * y - start y position (in symbols)
 * npanel - number of panels per window (2 for main file manager window)
 * handler - window handler function
 * text text 
 */
int init_window(window_t *pWindow, unsigned int height, unsigned int width,
                unsigned int ncol, unsigned int nrow, unsigned int x,
                unsigned int y, unsigned int npanel,
                int (*handler) (void *arg), char *text, window_t *parent) {
    pWindow->screen = (char *) efm_malloc(height * width);
    memset(pWindow->screen, 0, height * width);
    pWindow->win_handler = handler;
    pWindow->text = text;
    pWindow->height = height;
    pWindow->width = width;
    pWindow->cur_col = 1;
    pWindow->cur_row = 1;
    pWindow->s_x = x;
    pWindow->s_y = y;
    pWindow->num_panel = npanel;
    pWindow->active_panel = 0;
    pWindow->nelement = 0;

    pWindow->panel[0].cur_col = 1;
    pWindow->panel[0].cur_row = 1;
    pWindow->panel[0].num_col = ncol;
    pWindow->panel[0].num_row = nrow;
    pWindow->panel[0].panel_offset = 0;
    pWindow->panel[0].next = &pWindow->panel[1];
    pWindow->panel[0].pn = 0;
    pWindow->panel[0].num_items = 0;
    pWindow->parent = parent;

    if (parent) 
        pWindow->backup_region = (char *) efm_malloc((pWindow->width + 10) * (pWindow->height + 10));
    else 
        pWindow->backup_region = NULL;

    if (npanel > 1) {
        pWindow->panel[1].cur_col = 1;
        pWindow->panel[1].cur_row = 1;
        pWindow->panel[1].num_col = ncol;
        pWindow->panel[1].num_row = nrow;
        pWindow->panel[1].next = &pWindow->panel[0];
        pWindow->panel[1].panel_offset = (pWindow->width / pWindow->num_panel) +
                                         1;
        pWindow->panel[1].pn = 1;
        pWindow->panel[1].num_items = 0;
    }
#if defined(CONFIG_EMBEDDED) || defined (OS_LINUX)
    strcpy(pWindow->panel[0].cur_path, EFM_DIR_SLASH_S);
    strcpy(pWindow->panel[1].cur_path, EFM_DIR_SLASH_S);
#else
    strcpy(pWindow->panel[0].cur_path, "C:"EFM_DIR_SLASH_S);
    strcpy(pWindow->panel[1].cur_path, "C:"EFM_DIR_SLASH_S);
#endif

    return 0;
}

/*
 * get current cursor row 
 */
int win_get_row(window_t *win) {
    return win->panel[win->active_panel].cur_row;
}

/*
 * get current cursor column 
 */
int win_get_col(window_t *win) {
    return win->panel[win->active_panel].cur_col;
}

/*
 * drow HORIZONTAL line 
 * win - window
 * x - x position
 * y - y position
 * len - line length
 */
int win_draw_h_line(window_t *win, unsigned int x, unsigned int y,
                    unsigned int len) {
    unsigned char col_el = '-';
    draw_line(win, &col_el, 1, T_HORIZONTAL, x, y, len);
    return 0;
}

/*
 * drow VERTICAL line 
 * win - window
 * x - x position
 * y - y position
 * len - line length
 */
int win_draw_v_line(window_t *win, unsigned int x, unsigned int y,
                    unsigned int len) {
    unsigned char col_el = cel;///*'|'*/179;
    draw_line(win, &col_el, 1, T_VERTICAL, x, y, len);
    return 0;
}

/*
 * set cursor position 
 */
int win_set_pos(window_t *win, unsigned int col, unsigned int row) {
    unsigned int ccol, crow;
    unsigned int col_offset = (win->width / win->num_panel) /
                              win->panel[win->active_panel].num_col;

    win->panel[win->active_panel].cur_col = col;
    win->panel[win->active_panel].cur_row = row;

    crow = win->s_y + 1 + row;
    ccol = win->s_x +
           1 +
           win->panel[win->active_panel].panel_offset +
           (col_offset * col);//ccol = win->s_x + 1 + (col_offset * col);
    // draw on screen
    ll_set_pos(win, ccol, crow);
    return 0;
}

/*
 * set cursor position DRY
 */
int win_set_pos_dry(window_t *win, unsigned int col, unsigned int row) {
    unsigned int ccol, crow;
    unsigned int col_offset = (win->width / win->num_panel) /
                              win->panel[win->active_panel].num_col;

    win->panel[win->active_panel].cur_col = col;
    win->panel[win->active_panel].cur_row = row;

    crow = win->s_y + 1 + row;
    ccol = win->s_x +
           1 +
           win->panel[win->active_panel].panel_offset +
           (col_offset * col);//ccol = win->s_x + 1 + (col_offset * col);
    // set current position instead of drawing on screen
    win->cur_pos_x = ccol;
    win->cur_pos_y = crow;
    return 0;
}

/*
 *
 */
int win_change_caption(window_t *win, char *text) {
    win->text = text;
    return 0;
}

/*
 * show current path in panels
 */
void win_show_path(window_t *win) {
    unsigned int panel;
    unsigned int panel_len = win->width / win->num_panel;
    char *buf = (char *)efm_malloc(256);
    unsigned int panel_offset = (win->s_x + (win->width / win->num_panel));
    int i, j, col_off, x_pos;

    if (!buf) {
        return;
    }

    draw_line(win, &llh, 1, T_HORIZONTAL, win->s_x + 1, win->s_y, win->width - 2);
    draw_line(win, &pd_u, 1, T_VERTICAL, panel_offset, win->s_y, 1);

    for (i = 0; i < win->num_panel; i++) {
        col_off = (win->width / win->num_panel) / win->panel[i].num_col;
        for (j = 0; j < win->panel[i].num_col - 1; j++) {
            x_pos = (win->panel[i].panel_offset) + (col_off * (j + 1));
            draw_line(win, &cd_u, 1, T_VERTICAL, win->s_x + x_pos, win->s_y, 1);
        }      
        x_pos = 0;
    }

    for (panel = 0; panel < win->num_panel; panel++) {
        if (strlen(win->panel[panel].cur_path) > 0)
            sprintf(buf, "< %*s >",(strlen(win->panel[panel].cur_path) >(panel_len - 2) ?
                     panel_len - 2 :strlen(win->panel[panel].cur_path)),win->panel[panel].cur_path);
        else
#if !defined(OS_LINUX)
            sprintf(buf, "< %s >", EFM_DIR_SLASH_S);
#else
            sprintf(buf, "< %s >", "/");
#endif
        draw_text(win, win->s_x +(((win->panel[panel].panel_offset + (panel_len / 2))) -
                   ((strlen(buf) > (panel_len - 2) ?panel_len -2 :strlen(buf)) /2)),
                   win->s_y, buf,(strlen(buf) > (panel_len - 2) ? panel_len - 1 : strlen(buf)),T_NORMAL);
    }

    efm_free(buf);
}


/*
 * draw window with its elements
 */
int win_draw_main_win(window_t *win) {
    unsigned int i, j;
    unsigned int panel_offset = (win->s_x + (win->width / win->num_panel));
    unsigned int col_off;
    unsigned int x_pos = 0;
    unsigned char col_el = llvp;

    // draw main window rectangle
    draw_rectangle(win, win->s_x, win->s_y, win->width, win->height);
    // draw panels
    if (win->num_panel > 1) {
        draw_line(win, &pd_u, 1, T_VERTICAL, panel_offset, win->s_y, 1);
        for (i = 0; i < win->num_panel - 1; i++) {
            draw_line(win, &col_el, 1, T_VERTICAL, panel_offset, win->s_y+1, win->height-2);
        }    
        draw_line(win, &pd_l, 1, T_VERTICAL, panel_offset, win->s_y + win->height-1, 1);
    }
    // draw colomn in panels
    x_pos = 0;
    for (i = 0; i < win->num_panel; i++) {
        col_off = (win->width / win->num_panel) / win->panel[i].num_col;
        for (j = 0; j < win->panel[i].num_col - 1; j++) {
            x_pos = (win->panel[i].panel_offset) + (col_off * (j + 1));
            win_draw_v_line(win, win->s_x + x_pos, win->s_y, win->height - 1);
            draw_line(win, &cd_l, 1, T_VERTICAL, win->s_x + x_pos, win->s_y + win->height-1, 1);
        }      
        x_pos = 0;
    }
    // draw elements
    for (i = 0; i < win->nelement; i++) {
        element_t *el = (element_t *) win->element[i];
        if (el->element_type == BUTTON) {
            button_t *b = (button_t *) el->pelement;
            draw_rectangle(win, b->x, b->y, b->w, b->h);
            draw_text(win, b->c_x, b->c_y, b->caption, strlen(b->caption),
                      T_NORMAL);
        } else if (el->element_type == EDIT) {
            edit_t *e = (edit_t *) el->pelement;
            draw_rectangle(win, e->x, e->y, e->w, e->h);
        }
    }
    // draw text if exists...
    if (win->text != NULL) {
        draw_text(win, win->s_x + 1, win->s_y + 1, win->text,
                  strlen(win->text), T_NORMAL);
    }
    return 0;
}

/*
 * write text to the specific panel
 * win - window
 * text - text to write
 * panel - panel to write
 * col - column
 * row - row
 * opt - horizontal or vertical text
 */
int win_write_text(window_t *win, char *text, unsigned int panel,
                   unsigned int col, unsigned int row, unsigned int opt) {
    unsigned int crow, ccol;
    unsigned int col_offset = (win->width / win->num_panel) /
                              win->panel[win->active_panel].num_col;
    unsigned int len = strlen(text);

    crow = win->s_y + 1 + row;
    ccol = win->s_x + 1 + win->panel[panel].panel_offset + (col_offset * col);
    win_set_pos(win, col, row);
    draw_text(win, ccol, crow, text, len, opt);

    return 0;
}

/*
 * write text to the screen buffer only of the specific panel (need to be printed then)
 */
int win_write_text_dry(window_t *win, char *text, unsigned int panel,
                   unsigned int col, unsigned int row, unsigned int opt) {
    unsigned int crow, ccol;
    unsigned int col_offset = (win->width / win->num_panel) /
                              win->panel[win->active_panel].num_col;
    unsigned int len = strlen(text);

    crow = win->s_y + 1 + row;
    ccol = win->s_x + 1 + win->panel[panel].panel_offset + (col_offset * col);
    win_set_pos_dry(win, col, row);
    draw_text_dry(win, ccol, crow, text, len, opt);

    return 0;
}

int unescape_text(char *buf) {
    char *b;
    char *c, *d;
    int len = strlen(buf);
    if (len <= 0) return 0;

    c = strchr(buf, '[');
    if (!c) return 0;
    d = strchr(c, ']');
    if (!d) return 0;
    *d = '\0';
    strcpy(buf, ++c);
    return 0;
}

/*
 * read text
 * win - window
 * text - buffer to read
 * panel - panel to read from
 * col - column
 * row - row
 * len - length
 */
int win_read_text(window_t *win, char *text, unsigned int panel,
                  unsigned int col, unsigned int row, unsigned int len) {
    unsigned int crow, ccol;
    unsigned int col_offset = (win->width / win->num_panel) /
                              win->panel[win->active_panel].num_col;

    crow = win->s_y + 1 + row;
    ccol = win->s_x + 1 + win->panel[panel].panel_offset + (col_offset * col);
    win_set_pos(win, col, row);
    win_read(win, text, &crow, &ccol, len);

    return 0;
}

/* 
 * fast read text
 */ 
int win_read_text2(window_t *win, char *text, unsigned int panel,
                  unsigned int col, unsigned int row, unsigned int len) {
    unsigned int crow, ccol;
    unsigned int col_offset = (win->width / win->num_panel) /
                              win->panel[win->active_panel].num_col;

    crow = win->s_y + 1 + row;
    ccol = win->s_x + 1 + win->panel[panel].panel_offset + (col_offset * col);
    win_read(win, text, &crow, &ccol, len);

    return 0;
}

/*
 * remove selection from text 
 */
int win_remove_selection(window_t *win, unsigned int col, unsigned int row) {
    char buf[20];

    memset(buf, 0, 20);
    // remove selection
    win_set_pos(win, col, row);
    win_read_text2(win, buf, win->active_panel, col, row, 12);
    if (strlen(buf) <= 0)
        return -1;
    win_write_text(win, buf, win->active_panel, col, row, T_NORMAL);

    return 0;
}

int win_remove_cur_selection(window_t *win) {
    int ccol = win_get_col(win);
    int crow = win_get_row(win);

    return win_remove_selection(win, ccol, crow);
}

/*
 * find the last row in the colomn
 */
int win_find_last_row(window_t *win, unsigned int col, unsigned int row) {
    int r = row;
    int len;
    char buf[20];

    memset(buf, 0, 20);

    while (r >= 0) {
        win_read_text2(win, buf, win->active_panel, col, r, 12);
        if ((len = strlen(buf)) <= 0) {
            r--; continue;
        }
        return r;
    }
    return -1;
}


/*
 * set text  selected
 */
int win_set_selection(window_t *win, unsigned int col, unsigned int row) {
    char buf[20];
    int len;

    memset(buf, 0, 20);
    // remove selection
//    win_set_pos(win, col, row);
    win_read_text2(win, buf, win->active_panel, col, row, 12);
    if ((len = strlen(buf)) <= 0)
        return -1;
    win_write_text(win, buf, win->active_panel, col, row, T_INVERSE);

    return 0;
}

int win_set_cur_selection(window_t *win) {
    int ccol = win_get_col(win);
    int crow = win_get_row(win);

    return win_set_selection(win, ccol, crow);
}

/*
 * set active panel 
 */
void win_set_active_panel(window_t *win, unsigned int panel) {
    win->active_panel = panel;
    win_set_selection(win, 0, 0);
}

/*
 * change panel 
 */
int win_change_panel(window_t *win) {
    unsigned int next_panel = win->panel[win->active_panel].next->pn;

    win_remove_selection(win, win_get_col(win), win_get_row(win));
    win_set_active_panel(win, next_panel);

    return 0;
}

/*
 * clear panel
 */
void win_clear_panel(window_t *win, unsigned int panel) {
    unsigned int i, j;
    unsigned int ccol, crow;
    unsigned int col_offset = (win->width / win->num_panel) /
                              win->panel[win->active_panel].num_col;

    unsigned int offset, len = 12;
    char buf[20];

    for (i = 0; i < win->panel[panel].num_row; i++) {
        for (j = 0; j < win->panel[panel].num_col; j++) {
            memset(buf, 0, 20);
            win_set_pos(win, j, i);
            /*
            win_read_text(win, buf, panel, j, i, 12);
            len = strlen(buf);
            if (len <= 0)
                continue;
            */ 
            crow = win->s_y + 1 + i;
            ccol = win->s_x +
                   1 +
                   win->panel[panel].panel_offset +
                   (col_offset * j);
            // clear memory
            offset = ((win->width * (crow - win->s_y)) + (ccol - win->s_x));
            memset(&win->screen[offset], 0, len);
            memset(buf, ' ', len);
            buf[len] = 0;
            wr_str(buf);
        }
    }
}

static int panel_scroll_pos[2] = {0, 0};
/*
 * Reset scroll position
 */
static void reset_scroll(window_t *win) {
    panel_scroll_pos[win->active_panel] = 0;
    panel_scroll_pos[win->active_panel] = 0;
}

/*
 * scroll window
 */
static int win_scroll_dir(window_t *win, int direction) {
    int panel = win->active_panel;
    int num_items = 0;
    int type;
    int n;

    if (direction < 2) {
        type = 0;
    } else {
        type = 1;
    }

    num_items = (win->panel[panel].num_row * win->panel[panel].num_col) + panel_scroll_pos[panel];
    n = panel_scroll_pos[panel] % win->panel[panel].num_row;

    if (direction == 0) {
        if (num_items >= win->panel[panel].num_items - 1) {
            return -1;
        }

        if (++panel_scroll_pos[panel] >= 511) {
            panel_scroll_pos[panel] = 511;
            return 511;
        }

    } else if (direction == 1) {
        if (--panel_scroll_pos[panel] < 0) {
            panel_scroll_pos[panel] = 0;
            return 0;
        }
    } else if (direction == 2) {
        //scroll left <--
        int item_per_col = win->panel[panel].num_row;
        
        if (num_items >= win->panel[panel].num_items - 1) {
            return -1;
        }
       

        if (item_per_col + panel_scroll_pos[panel] >= 511) {
            panel_scroll_pos[panel] = 511;
            return 511;
        }
        // align to column
        panel_scroll_pos[panel] -= n;
        panel_scroll_pos[panel] += item_per_col;
    } else if (direction == 3) {
        int items_per_col = win->panel[panel].num_row;

        if (panel_scroll_pos[panel] - items_per_col < 0) {
            panel_scroll_pos[panel] = 0;
            return 0;
        }
        // align to column
        panel_scroll_pos[panel] -= n;
        panel_scroll_pos[panel] -= items_per_col;
    }
    return (panel_scroll_pos[panel] + (win->panel[panel].num_row * (win->panel[panel].num_col)));
}

/*
 * move cursor  up/down/right/left
 * with text selection under cursor
 */
int win_move_cursor(window_t *win, unsigned int dir) {
    unsigned int ccol, crow;
    int retval = 1, r;
    ccol = win_get_col(win);
    crow = win_get_row(win);

    if (dir == DIR_UP) {
        // up
        if ((crow + win->s_y) <= win->s_y/*need window offset*/) {
            if (ccol > 0) {
                win_remove_selection(win, ccol, crow);
                ccol--;
                crow = win->panel[win->active_panel].num_row - 1;
            } else {
                // scroll down and repaint
                if ((retval = win_scroll_dir(win, 1)) > 0) {
                    win_show_dir(win);
                }            
            }
            if (retval > 0) {
                win_set_selection(win, ccol, crow);
            }
        } else {
            win_remove_selection(win, ccol, crow);
            crow--;
            if (win_set_selection(win, ccol, crow)) {
                crow++;
                win_set_selection(win, ccol, crow);
                
            }
        }
    } else if (dir == DIR_DOWN) {
        // down
        if (crow >= win->panel[win->active_panel].num_row - 1) {
            if (ccol < win->panel[win->active_panel].num_col - 1) {
                win_remove_selection(win, ccol, crow);
                ccol++;
                crow = 0;
            } else {
                // scroll up and repaint
                win_scroll_dir(win, 0);
                win_show_dir(win);
            }
            win_set_selection(win, ccol, crow);
        } else {
            win_remove_selection(win, ccol, crow);
            crow++;
            if (win_set_selection(win, ccol, crow)) {
                crow--;
                win_set_selection(win, ccol, crow);
                
            }
        }
    } else if (dir == DIR_LEFT) {
        // left
        if (ccol > 0) {
            win_remove_selection(win, ccol, crow);
            ccol--;
            if (win_set_selection(win, ccol, crow)) {
                ccol++;
                win_set_selection(win, ccol, crow);
            }
        } else {
            if (win_scroll_dir(win, 3) > 0) {
                win_show_dir(win);
            }
            win_set_selection(win, ccol, crow);
        }
    } else if (dir == DIR_RIGHT) {
        //right

        if (ccol < win->panel[win->active_panel].num_col - 1) {
            win_remove_selection(win, ccol, crow);
            ccol++;

            if ((r = win_find_last_row(win, ccol, crow)) < 0) {
                ccol--;
                win_set_selection(win, ccol, crow);
            } else {
                win_set_selection(win, ccol, r);
            }
/*
            if (win_set_selection(win, ccol, crow)) {
                ccol--;
                win_set_selection(win, ccol, crow);
            }
*/ 
        } else {
            int n = win_scroll_dir(win, 2);
            if (n > 0 ) {
                win_show_dir(win);
                if (n > win->panel[win->active_panel].num_items) {
                    crow = 0;
                }
            } else {
                //win_remove_selection(win, ccol, crow);
                //crow = 0;
                //win_set_selection(win, ccol, crow);
            }
            win_set_selection(win, ccol, crow);
        }
    }
    return 0;
}

/*
 * update window
 */
static int win_update_win(window_t *win) {
    unsigned int s_x, s_y, p;
    unsigned int i, j;
    //char d[win->width];
    char *d = (char *) efm_malloc(win->width);
    unsigned int active_p;

    if (!d)
        return -1;

    active_p = win->active_panel;

    s_x = win->s_x;
    s_y = win->s_y;

    win_clear_screen(win);
    win_draw_main_win(win);
    for (p = 0; p < win->num_panel; p++) {
        win->active_panel = p;
        for (i = 0; i < win->panel[p].num_row; i++) {
            for (j = 0; j < win->panel[p].num_col; j++) {
                memset(d, 0, win->width);
                //win_set_pos(win, j, i);
                win_read_text2(win, d, p, j, i, 1);
                if (strlen(d) > 0)
                    win_write_text(win, d, p, j, i, T_NORMAL);
            }
        }
    }

    win_set_active_panel(win, active_p);
    efm_free(d);

    return 0;
}

/*
 * clear window
 */
static int win_clear_win(window_t *win) {
    unsigned int i, j, p, k;

    for (p = 0; p < win->num_panel; p++) {
        win->active_panel = p;
        for (i = 0; i < win->panel[p].num_row; i++) {
            for (j = 0; j < win->panel[p].num_col; j++) {
                win_set_pos(win, j, i);
                for (k = 0; k < win->width - 1; k++) {
                    wr_char(' ');
                }
            }
        }
    }
    return 0;
}

/*
 * clear region
 */
void win_clear_region(window_t *win) {
    unsigned int i,offset = 0, soffset, j;
    unsigned int sx, sy, ey;
    window_t *parent = win->parent;
    char *backup = win->backup_region;
    char *buf = (char *) efm_malloc(win->width + 5);

    if (!buf)
        return;

    sx = win->s_x - 1;
    sy = win->s_y - 1;
    ey = win->s_y + win->height;
    
    memset(buf, ' ', win->width + 3);
    buf[win->width + 3] = 0;
    
    for (i = sy; i < (ey + 2); i++) {
        if (backup && parent) {
            soffset = ((VT_DEC(i) - (VT_DEC(parent->s_y))) * parent->width) + VT_DEC(sx);
            memcpy(&backup[offset], &parent->screen[soffset], win->width + 3);
            for (j = 0;j < win->width + 3;j++) {
                if (!isprint(backup[offset + j])) backup[offset + j] = ' ';
            }
            offset += win->width + 3;
        }
        ll_set_pos(win, sx, i);
        wr_str(buf);
    }
    efm_free(buf);
}


/*
 * restore region
 */
void win_restore_region(window_t *win) {
    unsigned int i,offset = 0;
    unsigned int sx, sy, ey;
    char *buf;
    char *backup = win->backup_region;

    if (!backup)
        return;

    buf = (char *) efm_malloc(win->width + 5);

    if (!buf)
        return;

    sx = win->s_x - 1;
    sy = win->s_y - 1;
    ey = win->s_y + win->height;
    
    buf[win->width + 3] = 0;
    for (i = sy; i < (ey + 2); i++) {
        memcpy(buf, &backup[offset], win->width + 3);
        offset += win->width + 3;
        ll_set_pos(win, sx, i);
        wr_str(buf);
    }
    memset(backup, 0, (win->width + 10) * (win->height + 10));
    efm_free(buf); 
}

/*
 * draw shadow
 */
void win_draw_shadow(window_t *win) {
    unsigned int i;
    unsigned int sx, sy, ey;

    sx = win->s_x + win->width;
    sy = win->s_y + 1;
    ey = sy + win->height - 1;

    vt_set_mode(T_INVERSE);

    for (i = sy; i < ey; i++) {
        ll_set_pos(win, sx, i);
        wr_char(' ');
    }
    ll_set_pos(win, win->s_x + 1, ey);
    for (i = 0; i < win->width; i++) {
        wr_char(' ');
    }

    vt_set_mode(T_NORMAL);
}

int fs_item_is_dir(fs_dirent *direntp, char *path) {
#ifdef CONFIG_EMBEDDED
    return (direntp->fs_fflag & FS_ATTR_DIR);
#else
    char *dir = (char *) efm_malloc(256);
    struct stat fileStat;

    strcpy(dir, path);
    if (dir[strlen(dir) - 1] != EFM_DIR_SLASH_C)
        strcat(dir, EFM_DIR_SLASH_S);
    strcat(dir, direntp->fs_dname);
    stat(dir, &fileStat);
    direntp->fs_dname[12] = 0;
    efm_free(dir);
    return S_ISDIR(fileStat.st_mode);
#endif
}

/*
 * remove file
 */
int win_remove_file(window_t *win, char *name) {
    char pPath[128];
    unsigned int entrie;

    if (name[0] != EFM_DIR_SLASH_C) {
        strcpy(pPath, win->panel[win->active_panel].cur_path);
        entrie = find_last_entrie(pPath);
        pPath[entrie] = EFM_DIR_SLASH_C;
        strcpy(&pPath[entrie + 1], name);
    } else if (name[0] == EFM_DIR_SLASH_C) {
        strcpy(pPath, name);
    }

    return fs_fremove(pPath);
}

// recursive delete
int rremove_dir(window_t *win, char *path) {
    fs_dir *dirp = NULL;
    fs_dirent *direntp;
    char *s;
    unsigned int i, entrie;
#ifndef CONFIG_EMBEDDED
    //struct stat fileStat;
    //char *dir = (char *) efm_malloc(256);
#endif  
    dirp = fs_opendir(path);

    if (dirp) {
        do {
            direntp = fs_readdir(dirp);
            if (direntp) {
#ifdef CONFIG_EMBEDDED
                direntp->fs_dname[12] = 0;
                if (direntp->fs_fflag & FS_ATTR_DIR) {
#else
/*
                    strcpy(dir, path);
                    if (dir[strlen(dir) - 1] != EFM_DIR_SLASH_C)
                        strcat(dir, EFM_DIR_SLASH_S);
                    strcat(dir, direntp->fs_dname);
                    stat(dir, &fileStat);
                    direntp->fs_dname[12] = 0;
                    if (S_ISDIR(fileStat.st_mode)) {
*/
                    if (fs_item_is_dir(direntp, path)) {
                    
#endif
                        if (direntp->fs_dname[0] != '.') {
                            char *name = (char *) efm_malloc(12);

                            memcpy(name, direntp->fs_dname, 12);
                            name[8] = 0;
                            for (i = 0; i < 12; i++)
                                if (name[i] == ' ')
                                    name[i] = 0;
                            entrie = find_last_entrie(path);
                            path[entrie] = EFM_DIR_SLASH_C;
                            strcpy(&path[entrie + 1], name);
                            efm_free(name);
                            rremove_dir(win, path);
                        }
                    } else {
                        entrie = find_last_entrie(path);
                        path[entrie] = EFM_DIR_SLASH_C;
                        strcpy(&path[entrie + 1], direntp->fs_dname);
                        // to the win

                        s = efm_malloc(win->width);
                        memset(s, ' ', win->width - 2);
                        s[win->width - 2] = 0;
                        draw_text(win, win->s_x + 1, win->s_y + 1, s,
                                  strlen(s), T_NORMAL);
                        efm_free(s);

                        draw_text(win, win->s_x +
                                  1, win->s_y +
                                  1, path,
                                  ((strlen(path) > (win->width - 2)) ?
                                   win->width -
                                   2 :
                                   strlen(path)),
                                  T_NORMAL);
                        fs_fremove(path);
                        last_entrie(path, strlen(path));
                    }
                }
            } while (direntp);

            fs_closedir(dirp);
            // to the win
            s = efm_malloc(win->width);
            memset(s, ' ', win->width - 2);
            s[win->width - 2] = 0;
            draw_text(win, win->s_x + 1, win->s_y + 1, s, strlen(s), T_NORMAL);
            efm_free(s);

            draw_text(win, win->s_x +
                      1, win->s_y +
                      1, path,
                      ((strlen(path) > (win->width - 2)) ?
                       win->width -
                       2 :
                       strlen(path)),
                      T_NORMAL);
            fs_rmdir(path);
            last_entrie(path, strlen(path));
        }   
#ifndef CONFIG_EMBEDDED
        //efm_free(dir);
#endif  

        return 0;
    }

    /*
     * remove item with specified name
     */
    int win_remove_item(window_t *win, char *name, window_t *rem_win) {
        fs_dir *dirp = NULL;
    fs_dirent *direntp;
    unsigned int i, entrie;
    char *buf = (char *) efm_malloc(512);
#ifndef CONFIG_EMBEDDED
    //char *dir = (char *) efm_malloc(256);
    //struct stat fileStat;
#endif
    if (!buf)
        return -1;

    dirp = fs_opendir(win->panel[win->active_panel].cur_path);

    if (dirp) {
        do {
            direntp = fs_readdir(dirp);
            if (direntp) {
                //              direntp->fs_dname[12] = 0;
#ifdef CONFIG_EMBEDDED
                if (!memcmp(direntp->fs_dname, name, 12)) {
                    //fs_closedir(dirp);

                    direntp->fs_dname[12] = 0;
                    if (direntp->fs_fflag & FS_ATTR_DIR) {
#else

                        if (!strcmp(direntp->fs_dname, name)) {
                            /*
                            //fs_closedir(dirp);
                            strcpy(dir, win->panel[win->active_panel].cur_path);
                            if (dir[strlen(dir) - 1] != EFM_DIR_SLASH_C)
                                strcat(dir, EFM_DIR_SLASH_S);
                            strcat(dir, direntp->fs_dname);
                            stat(dir, &fileStat);

                            direntp->fs_dname[12] = 0;
                            if (S_ISDIR(fileStat.st_mode)) {
*/
                        if (fs_item_is_dir(direntp, win->panel[win->active_panel].cur_path)) {
#endif
                                name[8] = 0;
                                for (i = 0; i < 12; i++)
                                    if (name[i] == ' ')
                                        name[i] = 0;
                                strcpy(buf,
                                       win->panel[win->active_panel].cur_path);
                                entrie = find_last_entrie(buf);
                                buf[entrie] = EFM_DIR_SLASH_C;
                                strcpy(&buf[entrie + 1], name);
                                rremove_dir(rem_win, buf);
                            } else {
                                int ret = -1;
                                ret = win_remove_file(win, name);
                                efm_free(buf);
#ifndef CONFIG_EMBEDDED
                                //efm_free(dir);
#endif  
                                fs_closedir(dirp);
                                return ret;
                            }
                        }
                    }
                } while (direntp);
                fs_closedir(dirp);
            }
            efm_free(buf);
#ifndef CONFIG_EMBEDDED
            //efm_free(dir);
#endif  
            return 0;
}
/*
 * remove item under cursor (DEL)
 */
int win_remove_cur_item(window_t *win, window_t *rem_win) {
        unsigned int col, row;
        char buf[15];

        col = win_get_col(win);
        row = win_get_row(win);

        memset(buf, 0, 15);
        win_read_text(win, buf, win->active_panel, col, row, 12);
        if (strlen(buf) <= 0)
            return -1;
        if (buf[0] == '.')
            return -1;
        buf[12] = 0;

        return win_remove_item(win, buf, rem_win);
}

/*
 * copy file to another panel path
 */
int win_copy_file(window_t *win, char *name) {
        
        unsigned int col, row, entrie, x;
    char item_buf[50];
    char *path_from = (char *) efm_malloc(128);
    char *path_to = (char *) efm_malloc(128);
    fs_file *fFrom, *fTo;
    char *file_buf;

    if (!path_from || !path_to)
        return -1;

    col = win_get_col(win);
    row = win_get_row(win);

    memset(item_buf, 0, 12);

    win_read_text(win, item_buf, win->active_panel, col, row, 12);
    if (strlen(item_buf) <= 0) {
        efm_free(path_from);
        efm_free(path_to);
        return -1;
    }
    if (item_buf[0] == '.') {
        efm_free(path_from);
        efm_free(path_to);
        return -1;
    }
    item_buf[12] = 0;

    strcpy(path_from, win->panel[win->active_panel].cur_path);
    strcpy(path_to,
           (win->active_panel ==
            0 ?
            win->panel[1].cur_path :
            win->panel[0].cur_path));


    entrie = find_last_entrie(path_from);
    path_from[entrie] = EFM_DIR_SLASH_C;
    strcpy(&path_from[entrie + 1], item_buf);

    entrie = find_last_entrie(path_to);
    path_to[entrie] = EFM_DIR_SLASH_C;
    strcpy(&path_to[entrie + 1], item_buf);

    if (!strcmp(path_from, path_to)) {
        efm_free(path_from);
        efm_free(path_to);
        return -2; // The same path     
    }   

    fFrom = fs_fopen(path_from, "r");
    if (!fFrom) {
        efm_free(path_from);
        efm_free(path_to);
        return -1;
    }   
    fTo = fs_fopen(path_to, "w");
    if (!fFrom) {
        fs_fclose(fFrom);
        efm_free(path_from);
        efm_free(path_to);
        return -1;
    }
    file_buf = (char *) efm_malloc(1024);
    if (!file_buf) {
        fs_fclose(fFrom);
        fs_fclose(fTo);
        efm_free(path_from);
        efm_free(path_to);
        return -1;
    }
    do {
        x = fs_fread(file_buf, 1, 1024, fFrom);
        fs_fwrite(file_buf, 1, x, fTo);
    } while (x);

    fs_fclose(fFrom);
    fs_fclose(fTo);

    efm_free(path_from);
    efm_free(path_to);

    return 0;
}

/*
 * recursive copy directory to another location
 */
int rcopy_dir(window_t *win, char *path_from, char *path_to) {
    fs_dir *dirp = NULL;
    fs_dirent *direntp;
    int x;
    char name[15];
    char *file_buf, *s;
#ifndef CONFIG_EMBEDDED
    //char *dir = efm_malloc(256);
    //struct stat fileStat;
#endif  
    dirp = fs_opendir(path_from);

    if (dirp) {
        s = (char *) efm_malloc(win->width);
        memset(s, ' ', win->width - 2);
        s[win->width - 2] = 0;
        draw_text(win, win->s_x + 1, win->s_y + 1, s, strlen(s), T_NORMAL);
        draw_text(win, win->s_x + 1, win->s_y + 2, s, strlen(s), T_NORMAL);
        efm_free(s);
        draw_text(win, win->s_x +
                  1, win->s_y +
                  1, path_from,
                  ((strlen(path_from) > (win->width - 2)) ?
                   win->width -
                   2 :
                   strlen(path_from)),
                  T_NORMAL);
        draw_text(win, win->s_x +
                  1, win->s_y +
                  2, path_to,
                  ((strlen(path_to) > (win->width - 2)) ?
                   win->width -
                   2 :
                   strlen(path_to)),
                  T_NORMAL);
        if (fs_mkdir(path_to)) {
            fs_closedir(dirp);
            return -1;
        }   

        do {
            direntp = fs_readdir(dirp);
            if (direntp) {
                //              direntp->fs_dname[12] = 0;          
                memcpy(name, direntp->fs_dname, 12);
#ifdef CONFIG_EMBEDDED
                direntp->fs_dname[12] = 0;
                if (direntp->fs_fflag & FS_ATTR_DIR) {
#else
/*
                    strcpy(dir, path_from);
                    if (dir[strlen(dir) - 1] != EFM_DIR_SLASH_C)
                        strcat(dir, EFM_DIR_SLASH_S);
                    strcat(dir, direntp->fs_dname);
                    stat(dir, &fileStat);
                    direntp->fs_dname[12] = 0;
                    if (S_ISDIR(fileStat.st_mode)) {
*/
                    if (fs_item_is_dir(direntp, path_from)) {
#endif
                        if (direntp->fs_dname[0] != '.') {
                            // validate name
                            name[12] = name[8] = 0;
                            // if dir then create it and reenter in to the func
                            add_entrie(path_from, name, 1);
                            add_entrie(path_to, name, 1);
                            rcopy_dir(win, path_from, path_to);
                        }
                    } else {
                        // it is file, copy it...
                        fs_file *fsrc, *fdst;
                        name[12] = 0;
                        add_entrie(path_from, name, 0);
                        add_entrie(path_to, name, 0);

                        s = efm_malloc(win->width);
                        memset(s, ' ', win->width - 2);
                        s[win->width - 2] = 0;
                        draw_text(win, win->s_x + 1, win->s_y + 1, s,
                                  strlen(s), T_NORMAL);
                        draw_text(win, win->s_x + 1, win->s_y + 2, s,
                                  strlen(s), T_NORMAL);
                        efm_free(s);
                        draw_text(win, win->s_x +
                                  1, win->s_y +
                                  1, path_from,
                                  ((strlen(path_from) > (win->width - 2)) ?
                                   win->width -
                                   2 :
                                   strlen(path_from)),
                                  T_NORMAL);
                        draw_text(win, win->s_x +
                                  1, win->s_y +
                                  2, path_to,
                                  ((strlen(path_to) > (win->width - 2)) ?
                                   win->width -
                                   2 :
                                   strlen(path_to)),
                                  T_NORMAL);

                        fsrc = fs_fopen(path_from, "r");
                        if (!fsrc) {
                            fs_closedir(dirp);
#ifndef CONFIG_EMBEDDED
                            //efm_free(dir);
#endif  
                            return -1;
                        }
                        fdst = fs_fopen(path_to, "w");

                        if (!fdst) {
                            fs_fclose(fsrc);
                            fs_closedir(dirp);
#ifndef CONFIG_EMBEDDED
                            //efm_free(dir);
#endif  
                            return -1;
                        }

                        file_buf = (char *) efm_malloc(1024);
                        if (!file_buf) {
                            fs_fclose(fsrc);
                            fs_fclose(fdst);
                            fs_closedir(dirp);
#ifndef CONFIG_EMBEDDED
                            //efm_free(dir);
#endif  
                            return -1;
                        }

                        do {
                            x = fs_fread(file_buf, 1, 1024, fsrc);
                            fs_fwrite(file_buf, 1, x, fdst);
                        } while (x);

                        efm_free(file_buf);
                        fs_fclose(fsrc);
                        fs_fclose(fdst);
                        // return path
                        last_entrie(path_from, strlen(path_from));
                        last_entrie(path_to, strlen(path_to));
                    }
                }
            } while (direntp);
            fs_closedir(dirp);
#ifndef CONFIG_EMBEDDED
            //efm_free(dir);
#endif  
            last_entrie(path_from, strlen(path_from));
            last_entrie(path_to, strlen(path_to));
        }

        return -1;
}

/*
 * copy item with specific name
 */
int win_copy_item(window_t *win, char *name, window_t *rem_win) {
    fs_dir *dirp = NULL;
    fs_dirent *direntp;
    char *buf_from = (char *) efm_malloc(512);
    char *buf_to = (char *) efm_malloc(512);
#ifndef CONFIG_EMBEDDED
    //struct stat fileStat;
#endif  
    if (!buf_from || !buf_to)
        return -1;

    dirp = fs_opendir(win->panel[win->active_panel].cur_path);
    
    if (dirp) {
        do {
            direntp = fs_readdir(dirp);
            if (direntp) {
                //              direntp->fs_dname[12] = 0;
#ifdef CONFIG_EMBEDDED
                if (!memcmp(direntp->fs_dname, name, 12)) {
                    closedir(dirp);

                    direntp->fs_dname[12] = 0;
                    if (direntp->fs_fflag & FS_ATTR_DIR) {
#else

                        if (!strcmp(direntp->fs_dname, name)) {
/*
                            strcpy(buf_from,
                                   win->panel[win->active_panel].cur_path); 
                            if (buf_from[strlen(buf_from) - 1] != EFM_DIR_SLASH_C)
                                strcat(buf_from, EFM_DIR_SLASH_S);
                            strcat(buf_from, direntp->fs_dname);
                            stat(buf_from, &fileStat);
                            direntp->fs_dname[12] = 0;
                            if (S_ISDIR(fileStat.st_mode)) {
*/
                        if (fs_item_is_dir(direntp, win->panel[win->active_panel].cur_path)) {
#endif
                                name[8] = 0;
                                strcpy(buf_from,
                                       win->panel[win->active_panel].cur_path);
                                strcpy(buf_to,
                                       (win->active_panel ==
                                        0 ?
                                        win->panel[1].cur_path :
                                        win->panel[0].cur_path));
                                // the same path
                                if (!strcmp(buf_from, buf_to)) {
                                    efm_free(buf_from);
                                    efm_free(buf_to);
                                    fs_closedir(dirp);
                                    return -2;
                                }                           
                                add_entrie(buf_from, name, 1);
                                add_entrie(buf_to, name, 1);    
                                rcopy_dir(rem_win, buf_from, buf_to);
                            } else {
                                int res = win_copy_file(win, name);
                                if (res) {
                                    efm_free(buf_from);
                                    efm_free(buf_to);
                                    fs_closedir(dirp);
                                    return res;
                                }
                            }
                        }
                    }
                } while (direntp);
                fs_closedir(dirp);
            }

            efm_free(buf_from);
            efm_free(buf_to);

            return -1;
}

/*
 * copy item under cursor (F4)
 */
int win_copy_cur_item(window_t *win, window_t *rem_win) {
    unsigned int col, row;
    char buf[15];

    col = win_get_col(win);
    row = win_get_row(win);

    memset(buf, 0, 15);
    win_read_text(win, buf, win->active_panel, col, row, 12);
    if (strlen(buf) <= 0)
        return -1;
    if (buf[0] == '.')
        return -1;
    buf[12] = 0;

    return win_copy_item(win, buf, rem_win);
}

/*
 * create new directory in current panel (CTRL+N)
 */
int win_mk_dir(window_t *win, char *name) {
        char pPath[128];
    unsigned int entrie = 0;

    if (name[0] != EFM_DIR_SLASH_C) {
        strcpy(pPath, win->panel[win->active_panel].cur_path);
        entrie = find_last_entrie(pPath);
        pPath[entrie] = EFM_DIR_SLASH_C;
        strcpy(&pPath[entrie + 1], name);
    } else if (name[0] == EFM_DIR_SLASH_C) {
        strcpy(pPath, name);
    }

    return fs_mkdir(pPath);
}

/*
 * create new directory in current panel (CTRL+N)
 */
int win_mk_file(window_t *win, char *name) {
        char pPath[128];
    unsigned int entrie = 0;

    if (name[0] != EFM_DIR_SLASH_C) {
        strcpy(pPath, win->panel[win->active_panel].cur_path);
        entrie = find_last_entrie(pPath);
        pPath[entrie] = EFM_DIR_SLASH_C;
        strcpy(&pPath[entrie + 1], name);
    } else if (name[0] == EFM_DIR_SLASH_C) {
        strcpy(pPath, name);
    }

    return fs_fopen(pPath,"w");
}

/*
 * 
 */
void validate_name(fs_dirent *direntp, char *path) {
    char *name = direntp->fs_dname;
    int len = strlen(name);
    int i, pos = -1, space = 0;
    int is_dir =  fs_item_is_dir(direntp, path);

    if (is_dir) {
        if (len > 12) {
            sprintf(name + 11, "%s", "~");
        }
    } else {
        for (i = 0;i < len;i++) {
            if (name[i] == '.' || name[i] == ' ') {
                pos = i;
                break;
            }
        }
        if (pos > 8) {
            // name greater then 8 symbols
            sprintf(name + 6, "%s", "~1.");
            sprintf(name + 9, "%3s", &name[pos + 1]);
            name[12] = 0;
        }
    }
}

static char dir_context[2][512][20];
static char *dir_ptr_ctx[2][512]; 
/*
 * Rad directory items
 */
int win_read_dir_context(window_t *win, char *dir) {
    unsigned int row = 0, col = 0;
    fs_dir *dirp = NULL;
    fs_dirent *direntp;
    unsigned int cur_dirent = 0;
    dirp = fs_opendir(dir);
    if (dirp) {
        do {
            direntp = fs_readdir(dirp);
            if (direntp) {
                                
                validate_name(direntp, dir);
                //if (fs_item_is_dir(direntp, dir) && !strchr(direntp->fs_dname, '.')) {
                //    sprintf(&dir_context[win->active_panel][cur_dirent][0], "[%s]", direntp->fs_dname);
                //} else {
                    strcpy(&dir_context[win->active_panel][cur_dirent][0], direntp->fs_dname);
                //}
                dir_ptr_ctx[win->active_panel][cur_dirent] = &dir_context[win->active_panel][cur_dirent][0];

                if (++cur_dirent >= 511) {
                     strcpy(&dir_context[win->active_panel][cur_dirent][0], "");
                     fs_closedir(dirp);
                     win->panel[win->active_panel].num_items = cur_dirent;
                     return 0;
                }
            }
        } while (direntp);
        win->panel[win->active_panel].num_items = cur_dirent;
        strcpy(&dir_context[win->active_panel][cur_dirent][0], "");
        fs_closedir(dirp);
    } else
        return -1;

    return 0;
}

#define QUICKIE_STRCMP(a, b)  (*(a) != *(b) ? \
  (int) ((unsigned char) *(a) - \
         (unsigned char) *(b)) : \
  strcmp((a), (b)))

static void win_sort_names(char *name[], int cnt) {
    char temp[20];
    int i, j;

    for(i=0; i < cnt - 1 ; i++) {
        for(j=i+1; j< cnt; j++) {
            if(QUICKIE_STRCMP(name[i],name[j]) > 0) {
                strcpy(temp,name[i]);
                strcpy(name[i],name[j]);
                strcpy(name[j],temp);
            }
        }
    }
}


static int win_show_dir(window_t *win) {
    unsigned int row = 0, col = 0;
    int cur_dirent = 0;

    win_clear_panel(win, win->active_panel);
    win_show_path(win);

    cur_dirent = panel_scroll_pos[win->active_panel];

    win_sort_names(&dir_ptr_ctx[win->active_panel][cur_dirent], win->panel[win->active_panel].num_items - cur_dirent);

    while (1) {
        
        win_write_text(win, &dir_context[win->active_panel][cur_dirent][0], win->active_panel, col, row, T_NORMAL);
        cur_dirent++;
        if (row > win->panel[win->active_panel].num_row - 2) {
            row = 0;
            col++;
            if (col > win->panel[win->active_panel].num_col - 1) {
                col = 0;
                win_set_pos(win, 0, 0);
                //return -1;
                return 0;
            }
            win_set_pos(win, col, row);
        } else
            row++;
        if ((cur_dirent >= (win->panel[win->active_panel].num_items))) {
            win_set_pos(win, 0, 0);
            return 0;
        }
    }
    return 0;
}

/* 
 * fill in the screen buffer with the dir entries
 */
static int win_fill_dir_dry(window_t *win) {
    unsigned int row = 0, col = 0;
    int cur_dirent = 0;

    cur_dirent = panel_scroll_pos[win->active_panel];

    win_sort_names(&dir_ptr_ctx[win->active_panel][cur_dirent], win->panel[win->active_panel].num_items - cur_dirent);

    while (1) {
        
        win_write_text_dry(win, &dir_context[win->active_panel][cur_dirent][0], win->active_panel, col, row, T_NORMAL);
        cur_dirent++;
        if (row > win->panel[win->active_panel].num_row - 2) {
            row = 0;
            col++;
            if (col > win->panel[win->active_panel].num_col - 1) {
                col = 0;
                win_set_pos_dry(win, 0, 0);
                //return -1;
                return 0;
            }
            win_set_pos_dry(win, col, row);
        } else
            row++;
        if ((cur_dirent >= (win->panel[win->active_panel].num_items))) {
            win_set_pos_dry(win, 0, 0);
            return 0;
        }
    }
    return 0;
}

/* 
 * read directory and print it on the screen
 */
int win_read_dir_adv(window_t *win, char *dir) {

    reset_scroll(win);
    if (win_read_dir_context(win, dir) ) {
        return -1;
    }
    win_show_dir(win);
    return 0;
}

/* 
 * read directory and fill in only the screen buffer (need to be printed then)
 */
int win_read_dir_adv_dry(window_t *win, char *dir) {
    reset_scroll(win);

    if (win_read_dir_context(win, dir) ) {
        return -1;
    }
    win_fill_dir_dry(win);
    return 0;
}

/*
 * read directory and print it to current panel
 */
int win_read_dir(window_t *win, char *dir) {
    unsigned int row = 0, col = 0;
    fs_dir *dirp = NULL;
    fs_dirent *direntp;

    dirp = fs_opendir(dir);
    if (dirp) {
        win_clear_panel(win, win->active_panel);
        win_show_path(win);
        do {
            direntp = fs_readdir(dirp);
            if (direntp) {
                if (strlen(direntp->fs_dname) > 12) {
                    direntp->fs_dname[12] = '~';
                    direntp->fs_dname[13] = 0;
                } else {
                    direntp->fs_dname[12] = 0;
                }
                
                win_write_text(win, direntp->fs_dname, win->active_panel, col,
                               row, T_NORMAL);
                if (row > win->panel[win->active_panel].num_row - 2) {
                    row = 0;
                    col++;
                    if (col > win->panel[win->active_panel].num_col - 1) {
                        col = 0;
                        fs_closedir(dirp);
                        win_set_pos(win, 0, 0);
                        //return -1;
                        return 0;
                    }
                    win_set_pos(win, col, row);
                } else
                    row++;
            }
        } while (direntp);
        fs_closedir(dirp);
    } else
        return -1;

    return 0;
}

/*
 * determine if item with cpecific name is directory
 */
int win_item_is_dir(window_t *win, char *dirname) {
    fs_dir *dirp = NULL;

    dirp = fs_opendir(dirname);

    if (dirp) {
        fs_closedir(dirp);
        return 1;
    }
    return 0;
}

/*
 * enter to the directory
 */
int win_enter_dir(window_t *win) {
    unsigned int col, row;
    char buf[50];
    unsigned int entrie = 0, i;

    col = win_get_col(win);
    row = win_get_row(win);

    memset(buf, 0, 12);

    win_read_text(win, buf, win->active_panel, col, row, 12);
    if (strlen(buf) <= 0)
        return -1;
    if (buf[8] == ' ') {
        buf[8] = 0;
    }
    for (i = 0; i < strlen(buf); i++) {
        if (buf[i] == ' ')
            buf[i] = 0;
    }

    if (!strcmp(buf, "..")) {
        // previous directory
        last_entrie(win->panel[win->active_panel].cur_path, 128);
    } else if (!strcmp(buf, ".")) {
        // root directory
        //strcpy(win->panel[win->active_panel].cur_path, EFM_DIR_SLASH_S);
        set_root(win->panel[win->active_panel].cur_path, 128);
    } else {
        // selected item
        entrie = find_last_entrie(win->panel[win->active_panel].cur_path);
        win->panel[win->active_panel].cur_path[entrie] = EFM_DIR_SLASH_C;
        strcpy(&win->panel[win->active_panel].cur_path[entrie + 1], buf);
    }

    if (!win_read_dir_adv(win, win->panel[win->active_panel].cur_path)) {
        win_set_active_panel(win, win->active_panel);
    } else {
        last_entrie(win->panel[win->active_panel].cur_path, 128);
        // it is file
        return 1;
    }

    return 0;
}
//******************************************************************

extern int ffmpeg_play(char *filename);

/*
 * 
 */
int win_play_file(window_t *win) {

    unsigned int col, row;
    char buf[50];
    unsigned int entrie = 0, i;

    col = win_get_col(win);
    row = win_get_row(win);

    memset(buf, 0, 12);

    win_read_text(win, buf, win->active_panel, col, row, 12);
    if (strlen(buf) <= 0)
        return -1;
#ifdef CONFIG_EMBEDDED
    if ((!strncmp(&buf[9], "AVI", 3)) ||
        (!strncmp(&buf[9], "avi", 3)) ||
        (!strncmp(&buf[9], "mpg", 3)) ||
        (!strncmp(&buf[9], "MPG", 3)) ||
        (!strncmp(&buf[9], "3GP", 3)) ||
        (!strncmp(&buf[9], "3gp", 3))) {
        ffmpeg_play(buf);
    }
#else
    //printf("Filename: [%s]\n", buf);
#endif  
    return 0;
}
//*************************************************************************

/*
 * 
 */
void set_term_colsize(unsigned int colsize) {
    if (colsize == 80) {
        wr_str("\x1B[?3l");
    } else {
        wr_str("\x1B[?3h");
    }
}

/*
 * get input
 */
static int get_input_state = 0;
static int get_input() {
    char c;
    int key = -1;

    while (1) {
        c = rd_char();

        switch (get_input_state) {
        case 0:
            if (c == 0x1B) {
                get_input_state = 1;
                continue;
            } else {
                return c;
            }
            break;
        case 1:
            if (c == 0x5B) {
                get_input_state = 2;
                continue;
            } else if (c == 0x4F) {
                get_input_state = 3;
                continue;
            } else if (c == 0x1B) {
                get_input_state = 0;
                return BTN_ESC;
            }
            break;
        case 2:
            // UP, DOWN, LEFT, RIGHT
            if (c == 'A') {
                key = BTN_UP;
            } else if (c == 'B') {
                key = BTN_DOWN;
            } else if (c == 'D') {
                key = BTN_LEFT;
            } else if (c == 'C') {
                key = BTN_RIGHT;
            } else if (c == 0x33) {
                get_input_state = 3;
                continue;
            }
            
            get_input_state = 0;
            return key;
        case 3:
            // F1, F2, F3, F4
            if (c == 'P') {
                key = BTN_F1;
            } else if (c == 'Q') {
                key = BTN_F2;
            } else if (c == 'R') {
                key = BTN_F3;
            } else if (c == 'S') {
                key = BTN_F4;
            }  else if (c == 0x7E) {
                key = BTN_DEL;
            }
            get_input_state = 0;
            return key;
            break;
        default:
            get_input_state = 0;
            break;
        }
    }
}

/*
 * 
 */
int edit_en_callback(void *parg) {
    el_msg_t *msg = (el_msg_t *) parg;
    unsigned int ch = msg->ch;
    window_t *win = msg->win;
    button_t *b;
    unsigned int opcode = msg->opcode;
    int retval = -1;
    int ndots = 0;

    while (1) {
        ch = get_input();

        switch (ch) {
        case BTN_LEFT:
        case BTN_RIGHT:
        case BTN_TAB:
            win_next_element(win);
            break;
        case BTN_ENTER:
            {
                edit_t *e = win_get_edit(win);

                b = get_ative_but(win);
                if (b) {                  
                    if (!strncmp(b->caption, "YES", 3)) {
                        if (e) {
                            if (msg->in) {
                                edit_get_text(win, e, msg->in);
                            }
                        }           
                        retval = 1;
                    }                
                } else {
                    if (e) {
                        if (msg->in) {                        
                            edit_get_text(win, e, msg->in);
                            if (strlen(msg->in) > 0) {
                                retval = 1;
                            }
                        }
                    }
                }
                return retval;
                break;
            }
        case BTN_ESC:
            return 0;
        default:
             {
                unsigned int nel;
                element_t *el;
                nel = win_get_active_element(win);
                if (nel < 0)
                    return -1;
                el = (element_t *) win->element[nel];           
                if (el->element_type == EDIT) {
                    edit_t *e = (edit_t *) el->pelement;
                    if (((ch >= 'A') && (ch <= '~')) || (ch == '.')) {
                        char c = ch;
                        if (c == '.') {
                            if (ndots > 0) continue;
                            ndots++;
                        }
                        edit_add_text(win, e, &c, 1);
                    } else if (ch == BTN_BACKSPACE) {
                        edit_del_text(win, e, 1);
                    }
                }
            }

            break;
        }
    }
    return 0;
}

/*
 * 
 */
int en_callback(void *parg) {
    el_msg_t *msg = (el_msg_t *) parg;
    unsigned int ch = msg->ch;
    window_t *win = msg->win;
    button_t *b;
    int retval = -1;
    while (1) {
        ch = get_input();

        switch (ch) {
        case BTN_LEFT:
        case BTN_RIGHT:
        case BTN_TAB:
            win_next_element(win);
            break;
        case BTN_ENTER:
            b = get_ative_but(win);
            if (!strncmp(b->caption, "YES", 3))
                retval = 1;
            return retval;
            break;
        case BTN_ESC:
            return 0;
        default:
            break;
        }
    }

    return 0;
}

/*
 * 
 */
void start_editor(window_t *win) {
    unsigned int col, row;
    char *buf = (char *) efm_malloc(128);
    char bbb[15];

    if (!buf)
        return;

    col = win_get_col(win);
    row = win_get_row(win);

    memset(buf, 0, 128);
    memset(bbb, 0, 15);
    strcpy(buf, win->panel[win->active_panel].cur_path);
    win_read_text(win, bbb, win->active_panel, col, row, 12);
    if (strlen(bbb) <= 0)
        return;
    if (bbb[0] == '.')
        return;
    add_entrie(buf, bbb, 0);
    if (win_item_is_dir(win, buf))
        return;
    win_clear_screen(win);
    dav(1, buf);
    efm_free(buf);
}

#define OP_NONE     0
#define OP_REMOVE   1
#define OP_RENAME   2
#define OP_MKDIR    3
#define OP_COPY     4
#define OP_VIEW     5

/*
 * 
 */
void efm_task(void *parg) {
    char ch;
    unsigned int dir = -1;
    unsigned int input_state = 0;
    window_t *pwindow, *nwin;
    window_t *active_win = NULL;
    el_msg_t msg;
    int ret = 0;
    unsigned int operation = OP_NONE;
    char tBuf_i[100];
    char tBuf_o[100];

    msg.in = (void *) tBuf_i;
    msg.out = (void *) tBuf_o;

    init_graphic(EFM_GRAPHIS_MODE);
    set_term_colsize(132);
    ll_clear_screen();

    pwindow = new_window();
    init_window(pwindow, 21, 100, /*3*/2, 19, 1, 1, 2, NULL, NULL, NULL);
    win_draw_main_win(pwindow);

    win_set_active_panel(pwindow, 0);
    win_read_dir_adv(pwindow, pwindow->panel[pwindow->active_panel].cur_path);
    win_set_active_panel(pwindow, 1);
    win_read_dir_adv(pwindow, pwindow->panel[pwindow->active_panel].cur_path);
    win_set_active_panel(pwindow, 0);
    win_show_path(pwindow);

    while (1) {
        ch = get_input();

        msg.ch = ch;

        if (ch == BTN_ENTER) {
            if (active_win == NULL) {
                if (win_enter_dir(pwindow) == 1) {
                    win_play_file(pwindow);
                }
            }
            continue;
        } else if (ch == BTN_DEL) {
            // delete file or dir
            if ((active_win == NULL) && operation == OP_NONE) {
                int err = 0;
                nwin = new_window();
                win_create_en_win(nwin, "Delete file or dir", pwindow);
                win_remove_cur_selection(pwindow);
                win_clear_region(nwin);
                win_draw_main_win(nwin);
                win_draw_shadow(nwin); 
                active_win = nwin;
                msg.win = active_win;
                win_set_element_sel(active_win, 0);
                operation = OP_REMOVE;

                if (active_win->win_handler((void *) &msg) <= 0) {
                    err = -1;
                } else {
                    err = win_remove_cur_item(pwindow, active_win);
                }

                win_restore_region(active_win);
                win_remove_en_win(active_win);
                //win_read_dir_adv(pwindow,
                //             pwindow->panel[pwindow->active_panel].cur_path);
                if (!err) {
                    win_read_dir_adv(pwindow, pwindow->panel[pwindow->active_panel].cur_path);
                }
                win_set_cur_selection(pwindow);
                //win_show_path(pwindow);
                operation = OP_NONE;
                active_win = NULL;
            }
            continue;
        } else if (ch == BTN_CTRL_N) {
            // Make Dir
            if ((active_win == NULL) && operation == OP_NONE) {
                int err = 0;
                vt_cursor_mode(T_CURSOR_ON);
                nwin = new_window();
                win_create_edit_en_win(nwin, "Create new folder or file (.)", pwindow);
                win_remove_cur_selection(pwindow);
                win_clear_region(nwin);
                win_draw_main_win(nwin);
                win_draw_shadow(nwin);
                active_win = nwin;
                operation = OP_MKDIR;
                msg.win = active_win;
                msg.opcode = operation;
                win_set_element_sel(active_win, 2);
                if (active_win->win_handler((void *) &msg) <= 0) {
                    err = -1;
                } else {
                    if (strlen(msg.in) > 0) {
                        if (strchr(msg.in, '.')) {
                            win_mk_file(pwindow, (char *) msg.in);
                        } else {
                            win_mk_dir(pwindow, (char *) msg.in);
                        }
                        err = 1;
                    }
                }
                vt_cursor_mode(T_CURSOR_OFF);
                
                win_restore_region(active_win);
                win_remove_en_win(active_win);
                
                if (err > 0) {
                    win_read_dir_adv(pwindow, pwindow->panel[pwindow->active_panel].cur_path);
                }
                win_set_cur_selection(pwindow);
                //win_read_dir_adv_dry(pwindow, pwindow->panel[pwindow->active_panel].cur_path);
                //win_update_win(pwindow);
                //win_show_path(pwindow);
                operation = OP_NONE;
                active_win = NULL;
            }
        } else if (ch == BTN_CTRL_Q) {
            // exit from file manager
            if ((active_win == NULL) && operation == OP_NONE) {
                win_clear_screen(pwindow);
                win_remove_en_win(pwindow);
                set_term_colsize(80);
#ifdef CONFIG_EMBEDDED
                OSTaskDel(OS_PRIO_SELF);
                while (1) {
                    OSTimeDly(100);
                }
#else
                return;
#endif
            }
        } else if (ch == BTN_F1 ||
                   ch == BTN_F2 ||
                   ch == BTN_F3 ||
                   ch == BTN_F4) {
            if ((active_win == NULL) && (operation == OP_NONE)) {
                if (ch == BTN_F1) {
                    // F1
                    nwin = new_window();
                    win_remove_cur_selection(pwindow);
                    win_create_help_win(nwin,
                                        "F1     - Help\n"
                                        "F2     - Nothing\n"
                                        "F3     - Edit\n"
                                        "F4     - Copy item under cursor\n"
                                        "DEL    - Delete item under cursor\n"
                                        "Ctrl+N - Create directory or file\n"""
                                        "         (depends on \'.\' in the name)\n"
                                        "Ctrl+Q - Exit\n", pwindow);
                    win_clear_region(nwin);
                    win_draw_main_win(nwin);
                    win_draw_shadow(nwin);
                    active_win = nwin;
                    msg.win = active_win;
                    win_set_element_sel(active_win, 0);
                    operation = OP_VIEW;

                    if (active_win->win_handler((void *) &msg) <= 0) {
                    } else {
                    }

                    win_restore_region(nwin);
                    win_remove_en_win(active_win);
                    win_set_cur_selection(pwindow);
                    //win_read_dir_adv_dry(pwindow, pwindow->panel[pwindow->active_panel].cur_path);
                    //win_update_win(pwindow);
                    win_show_path(pwindow);
                    operation = OP_NONE;
                    active_win = NULL;
                } else if (ch == BTN_F2) {
                    // F2 
                } else if (ch == BTN_F3) {
                        // Edit F3
                        vt_cursor_mode(T_CURSOR_ON);
                        set_term_colsize(80);
                        start_editor(pwindow);
                        set_term_colsize(132);
                        vt_cursor_mode(T_CURSOR_OFF);
                        //win_read_dir_adv(pwindow,
                        //         pwindow->panel[pwindow->active_panel].cur_path);
                        win_read_dir_adv_dry(pwindow, pwindow->panel[pwindow->active_panel].cur_path);
                        win_update_win(pwindow);    
                        win_show_path(pwindow);

                } else if (ch == BTN_F4) {
                        int err = 0;
                        // F4 Copy item under cursor
                        win_remove_cur_selection(pwindow);
                        nwin = new_window();
                        win_create_en_win(nwin, "Copy file or dir", pwindow);
                        win_clear_region(nwin);
                        //break;
                        win_draw_main_win(nwin);
                        win_draw_shadow(nwin);
                        active_win = nwin;
                        msg.win = active_win;
                        win_set_element_sel(active_win, 0);
                        operation = OP_COPY;

                        if (active_win->win_handler((void *) &msg) <= 0) {
                            err = -1;
                        } else {
                            if (win_copy_cur_item(pwindow, active_win) == -2) {
                                err = 1;
                            }
                        }

                        win_restore_region(active_win);
                        win_remove_en_win(active_win);

                        if (err == 1) {
                            nwin = new_window();
                            win_create_ok_win(nwin, "ERROR!\nAttempt to copy to the same location", pwindow);
                            win_clear_region(nwin);
                            win_draw_main_win(nwin);
                            win_draw_shadow(nwin);
                            active_win = nwin;
                            msg.win = active_win;
                            win_set_element_sel(active_win, 0);
                            active_win->win_handler((void *) &msg);
                            win_restore_region(active_win);
                            win_remove_en_win(active_win);
                        } else if (err == 0) {
                            win_set_active_panel(pwindow,(pwindow->active_panel == 0 ?1 :0));
                            win_read_dir_adv(pwindow, pwindow->panel[pwindow->active_panel].cur_path);
                            win_set_active_panel(pwindow,(pwindow->active_panel == 0 ?1 : 0));
                        }

                        win_set_cur_selection(pwindow);
                        //win_read_dir_adv_dry(pwindow, pwindow->panel[pwindow->active_panel].cur_path);
                        //win_update_win(pwindow);
                        //win_show_path(pwindow);
                        operation = OP_NONE;
                        active_win = NULL;
                }
            }
        } else {
            if (ch == BTN_UP ||
                ch == BTN_DOWN ||
                ch == BTN_LEFT ||
                ch == BTN_RIGHT) {
                if (active_win == NULL) {
                    win_move_cursor(pwindow, ch);
                    continue;
                }
            }
            msg.ch = ch;    
            if (ch == BTN_TAB) {
                if (active_win == NULL) {
                    win_change_panel(pwindow);
                    continue;
                }
            }
            if (active_win) {
                active_win->win_handler((void *) &msg);
            }
        }
    } // end of while
}


#ifdef CONFIG_EMBEDDED
#define ESM_TASK_PRIO       17
#define ESM_STK_SIZE        (1024*4)
unsigned int efm_stk[ESM_STK_SIZE];

void run_efm() {
    OS_TCB p_task_data;
    if (OSTaskCreateExt(efm_task, (void *) 0,
                        (OS_STK *) &efm_stk[ESM_STK_SIZE - 1], ESM_TASK_PRIO,
                        ESM_TASK_PRIO, (OS_STK *) &efm_stk[0], ESM_STK_SIZE,
                        (void *) 0, OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR) ==
        OS_ERR_NONE) {
        do {
            OSTimeDly(100);
        } while (OSTaskQuery(ESM_TASK_PRIO, &p_task_data) == OS_NO_ERR);
    } else {
        printf("ERROR to load efm\r\n");
    }
}
#else
void run_efm() {
    efm_task(NULL);
    exit(0);
}
#endif
