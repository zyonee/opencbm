include ../../include/common.mk

ifeq "$(CYGWIN)" ""

  CFLAGS+=-g -Wall -pedantic -std=c99 -I ../../lib/ -I../../include/ $(CFLAGS_EXTRA)
  LFLAGS+=$(LDFLAGS_EXTRA)
else
  # special handling for CYGWIN:

  CFLAGS+=-I $(LIBUSB_ROOT)/include -g -Wall -pedantic -I../../include/ $(CFLAGS_EXTRA)
  LFLAGS+=$(LDFLAGS_EXTRA) -L $(LIBUSB_ROOT)/lib/gcc -lusb
endif

# remove if you want to generate debugging information
LFLAGS+=-s

APP = xu1541_update$(EXE_SUFFIX)
OBJS = xu1541_update.o ihex.o flash.o
LFLAGS := -L../../lib/ -lxu1541$(LIB_WIN) $(LFLAGS)

.INTERMEDIATE: $(OBJS)

.PHONY:	all clean mrproper xu1541lib

all:
	$(MAKE) ../$(APP)

clean:
	rm -f *.o *~

mrproper: clean
	rm -f ../xu1541_update ../xu1541_update.exe

../$(APP): $(OBJS) xu1541lib
	$(CC) $(OBJS) -o $@ $(LFLAGS)

xu1541lib:
	$(MAKE) -C ../../lib/
