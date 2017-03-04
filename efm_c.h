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
#ifndef EFM_H_
#define EFM_H_

void com_wr_str(const char *str);
void com_wr_data(char *data, int len);
void com_wr_char(char c);
int com_get_char();

typedef int (*win_callback_t)(void *arg);

#define BUTTON             2
#define EDIT               3

#define BTN_LEFT        0x01
#define BTN_RIGHT       0x02
#define BTN_UP          0x03
#define BTN_DOWN        0x04


#define BTN_DEL         0x7F
#define BTN_BACKSPACE   0x08
#define BTN_TAB         0x09
#define BTN_ESC         0x1B

#define BTN_CTRL_N      0x0E
#define BTN_CTRL_Q      0x11

#define BTN_F1          0x05
#define BTN_F2          0x06
#define BTN_F3          0x07


#define DIR_UP          BTN_UP
#define DIR_DOWN        BTN_DOWN
#define DIR_LEFT        BTN_LEFT
#define DIR_RIGHT       BTN_RIGHT

#define VT_INC(X)       ((X) + 1)
#define VT_DEC(X)       ((X) - 1)


//#define NET_IO
//#define PC_IO
//#define CONFIG_EMBEDDED


#if defined(CONFIG_EMBEDDED)
#define EFM_GRAPHIS_MODE        1
#define EFM_DIR_SLASH_C         '\\'
#define EFM_DIR_SLASH_S         "\\"

#define BTN_ENTER       0x0D
#define BTN_F4          0x0A

// input/output functions
#define wr_str                  net_wr_str
#define wr_data                 net_wr_data
#define wr_char                 net_wr_char
#define rd_char                 net_get_char

// memory management functions
#define efm_malloc              safe_malloc
#define efm_free                safe_free

// console dependent functions
#define set_cursor_pos          vt_set_cursor_pos
#define clear_screen            vt_clear_screen
#define clear_line              vt_clear_line
#define set_mode                vt_set_mode

// FS dependent functions
#define fs_dir                  FS_DIR
#define fs_dirent               struct FS_DIRENT
#define fs_dname                DirName
#define fs_fflag                Attributes
#define FS_ATTR_DIR             FS_ATTR_DIRECTORY

#define fs_opendir(p)           FS_OpenDir(p)
#define fs_fremove(p)           FS_Remove(p)
#define fs_rmdir(p)             FS_RmDir(p)
#define fs_mkdir(p)             FS_MkDir(p)
#define fs_readdir(dp)          FS_ReadDir(dp)
#define fs_closedir(dp)         FS_CloseDir(dp);

#define fs_file                 FS_FILE
#define fs_fopen(p,o)           FS_FOpen(p,o)
#define fs_fclose(pf)           FS_FClose(pf)
#define fs_fread(pb,s,n,pf)     FS_FRead(pb,s,n,pf)
#define fs_fwrite(pb,s,n,pf)    FS_FWrite(pb,s,n,pf)

#elif defined(PC_IO)
#define EFM_GRAPHIS_MODE        1
#define EFM_DIR_SLASH_C         '\\'
#define EFM_DIR_SLASH_S         "\\"

#define BTN_ENTER       0x0D
#define BTN_F4          0x0A
// input/output functions
#define wr_str                  com_wr_str
#define wr_data                 com_wr_data
#define wr_char                 com_wr_char
#define rd_char                 com_get_char

// memory management functions
#define efm_malloc              malloc
#define efm_free                free

// console dependent functions
#define set_cursor_pos          vt_set_cursor_pos
#define clear_screen            vt_clear_screen
#define clear_line              vt_clear_line
#define set_mode                vt_set_mode

// FS dependent functions
#define fs_dir                  DIR
#define fs_dirent               struct dirent
#define fs_dname                d_name
#define fs_fflag                
#define FS_ATTR_DIR             DT_DIR

#define fs_opendir(p)           opendir(p)
#define fs_fremove(p)           remove(p)
#define fs_rmdir(p)             unlink(p)
#define fs_mkdir(p)             mkdir(p)
#define fs_readdir(dp)          readdir(dp)
#define fs_closedir(dp)         closedir(dp);

#define fs_file                 FILE
#define fs_fopen(p,o)           fopen(p,o)
#define fs_fclose(pf)           fclose(pf)
#define fs_fread(pb,s,n,pf)     fread(pb,s,n,pf)
#define fs_fwrite(pb,s,n,pf)    fwrite(pb,s,n,pf)

#else
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "term.h"
#define EFM_GRAPHIS_MODE        0
#define EFM_DIR_SLASH_C         '/'
#define EFM_DIR_SLASH_S         "/"

#define BTN_ENTER       0x0A
#define BTN_F4          0x0B
    // Embedded Linux
// input/output functions
#define wr_str                  com_wr_str
#define wr_data                 com_wr_data
#define wr_char                 com_wr_char
#define rd_char                 com_get_char

// memory management functions
#define efm_malloc              malloc
#define efm_free                free

// console dependent functions
#define set_cursor_pos          vt_set_cursor_pos
#define clear_screen            vt_clear_screen
#define clear_line              vt_clear_line
#define set_mode                vt_set_mode

// FS dependent functions
#define fs_dir                  DIR
#define fs_dirent               struct dirent
#define fs_dname                d_name
#define fs_fflag                
#define FS_ATTR_DIR             DT_DIR

#define fs_opendir(p)           opendir(p)
#define fs_fremove(p)           remove(p)
#define fs_rmdir(p)             rmdir(p)
#define fs_mkdir(p)             mkdir(p, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
#define fs_readdir(dp)          readdir(dp)
#define fs_closedir(dp)         closedir(dp);

#define fs_file                 FILE
#define fs_fopen(p,o)           fopen(p,o)
#define fs_fclose(pf)           fclose(pf)
#define fs_fread(pb,s,n,pf)     fread(pb,s,n,pf)
#define fs_fwrite(pb,s,n,pf)    fwrite(pb,s,n,pf)

#endif

typedef struct _panel_t {
    struct _panel_t *next;
    unsigned int num_col;
    unsigned int num_row;
    unsigned int cur_col;
    unsigned int cur_row;
    unsigned int panel_offset;
    unsigned int pn;
    unsigned int num_items;
    char cur_path[128];
} panel_t;

typedef struct window {
    // window h & h
    unsigned int height;
    unsigned int width;
    // cols & rows
    unsigned int num_col;
    unsigned int num_row;
    unsigned int cur_col;
    unsigned int cur_row;
    // start coord
    unsigned int s_x;
    unsigned int s_y;
    // screen offset
    unsigned int cur_pos_x;
    unsigned int cur_pos_y;
    // panels
    panel_t panel[2];
    unsigned int num_panel;
    unsigned int active_panel;
    // element
    void *element[10];
    unsigned int nelement;
    // handler
    int (*win_handler)(void *arg);
    // caption
    char *text;
    // screen buffer
    char *screen;
    // 
    char *backup_region;
    //
    struct window *parent;
} window_t;

typedef struct _element_t {
    unsigned int element_type;
    unsigned int active;
    void *pelement;
    struct _element_t *next;
} element_t;

typedef struct _button_t {
    unsigned int id;
    unsigned int x;
    unsigned int y;
    unsigned int w;
    unsigned int h;
    char *caption;
    unsigned int c_x;
    unsigned int c_y;
    unsigned int active;
    struct _button_t *next;
    win_callback_t button_func;
} button_t;

typedef struct _edit_t {
    unsigned int id;
    unsigned int x;
    unsigned int y;
    unsigned int w;
    unsigned int h;
    unsigned int cur_pos_x;
    unsigned int cur_pos_y;
    unsigned int len;
    unsigned int active;
    char *buf;
    char *caption;
    struct _edit_t *next;
    win_callback_t edit_func;
} edit_t;

typedef struct {
    window_t *win;
    unsigned int ch;
    void *in;
    void *out;
    unsigned int opcode;
} el_msg_t;

void run_efm();
void set_term_colsize(unsigned int colsize);



#endif /*EFM_H_*/

