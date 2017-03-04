EFM - Embedded File Manager.
----------------------------

EFM designed to make file management easy on embedded systems. EFM works on any systems
with or without OS. It can manage only one (default) file system and only one volume. EFM works
with VT100 or VT100J terminal programm (HyperTerminal in Windows or GTKTerm on Linux) in 80 and 132 column mode.
Runing on Windows station EFM is trying to open COM port (COM-6).

To run EFM on Windows station we use com0com Null-modem emulator. (http://com0com.sourceforge.net)
com0com is an advanced utility, which purpose is to emulate RS232 serial ports connected
via virtual null-modem cable. In other words, with com0com you can create pure virtual serial
ports in your system,  that will be connected to each other via virtual null-modem cable without having real 
serial ports occupied.

FEATURES:
1. View and edit text files.
EFM uses DAV text file editor to view and edit text files. DAV requires a lot of memory to load and 
store text data, that is why it is not suitable for memory-less systems. 
2. Walk through directories.
3. Create and remove directories and files.
4. Copy directories and files.
5. Scroll directorie view.
6. Run in 2 graphic modes.

PORTING:
EFM was successfully tested on i386 WindowsXP station under MinGW, Ubuntu Linux and Atmel's ARM9 evaluation board.
You can easily port EFM source code on Windows under MinGW or Cygwin, Linux or ARM using WinArm compiler.

To configure EFM for building you must edit the efm_c.h file.

default - build on a Linux box

    #define PC_IO - build on Windows
    #define CONFIG_EMBEDDED - build for embedded system such as uCos-II


COPYRIGHT:
  This program is copyrighted under the GNU General Public License.

CONTACT:
Yury Bobrov
Email: ubobrov@yandex.ru
