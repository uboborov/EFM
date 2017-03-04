

PATH=$PATH:/usr/local/angstrom/armv7linaro/bin/
APPBIN_PATH ?= ~/
LIBGCC_PATH = /usr/local/angstrom/armv7linaro/lib/gcc/arm-linux-gnueabihf/4.7.3
LIBC_PATH   = /usr/local/angstrom/armv7linaro/lib

ARCH ?= arm
OS_LINUX = yes

ifeq ($(ARCH),arm)
CROSS_COMPILE = arm-linux-gnueabihf-
else
CROSS_COMPILE = x86_64-pc-linux-gnu-
endif

CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
OCP = $(CROSS_COMPILE)objcopy
AS = $(CROSS_COMPILE)gcc -x assembler-with-cpp
AR = $(CROSS_COMPILE)ar

CP = /usr/bin/sudo /bin/cp

MKFILE = Makefile

SRC = dav.c EFM.c efm_c.c cport.c

ifeq ($(OS_LINUX),yes)
DEFS = -DOS_LINUX -DARCH_ARM
SRC += term.c
endif

ifeq ($(ARCH),arm)
CPFLAGS = $(MCFLAGS) $(DEFS) -mlittle-endian -mfloat-abi=hard -O2 -Wall -Wno-pointer-sign
else
CPFLAGS = $(MCFLAGS) $(DEFS) -g -O2 -Wall -m32
endif

CPFLAGS += -fno-strict-aliasing -lpthread -Ulinux -Dlinux=linux -I. -I/usr/local/include
LIBDIR  = -L$(LIBGCC_PATH) -L$(LIBC_PATH)

ifeq ($(ARCH),arm)
LDFLAGS = $(MCFLAGS) -g -O2 -Wl,-EL -lm -lc -lgcc -lrt -lpthread $(LIBDIR)
else
LDFLAGS = $(MCFLAGS) -g -O2 -lm -lrt -m32 -lpthread
endif

OBJS = $(SRC:.c=.o)
DEPS = $(SRC:.c=.d)

DEL = /bin/rm -f

.PHONY: all target
target: all

all: $(OBJS) $(MAKEFILE)
	$(CC) $(OBJS) -o efm $(LDFLAGS)
	-$(CP) efm $(APPBIN_PATH)
	
%.d: %.c $(MKFILE)
	@echo "Building dependencies for '$<'"
	@$(CC) -E -MM -MQ $(<:.c=.o) $(CPFLAGS) $< -o $@
	@$(DEL) $(<:.c=.o)

%.o: %.c $(MKFILE)
	@echo "Compiling '$<'"
	$(CC) -c $(CPFLAGS) -I . $< -o $@

clean:
	-$(DEL) $(OBJS:/=\)
	-$(DEL) $(DEPS:/=\)

.PHONY: dep
dep: $(DEPS) $(SRC)
	@echo "##########################"
	@echo "### Dependencies built ###"
	@echo "##########################"

-include $(DEPS)
