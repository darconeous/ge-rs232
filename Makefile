
INSTALL=install
PREFIX=/usr/local

GE_RS232_SOURCE_PATH=.
GE_RS232_SOURCE_FILES=ge-rs232.c


ifdef VERBOSE_DEBUG
CFLAGS+=-DVERBOSE_DEBUG=$(VERBOSE_DEBUG)
endif

ifeq ($(DEBUG),1)
CFLAGS+=-O0 -g
CFLAGS+=-DDEBUG=1
endif

#CFLAGS+=-I.

GE_RS232_OBJECT_FILES=${addprefix $(GE_RS232_SOURCE_PATH)/,${subst .c,.o,$(GE_RS232_SOURCE_FILES)}}

GE_RS232D_SOURCE_PATH=.
GE_RS232D_SOURCE_FILES=main.c
GE_RS232D_OBJECT_FILES=${addprefix $(GE_RS232D_SOURCE_PATH)/,${subst .c,.o,$(GE_RS232D_SOURCE_FILES)}}

.PHONY: all test install uninstall clean

all: ge-rs232d

ge-rs232d: $(GE_RS232D_OBJECT_FILES) $(GE_RS232_OBJECT_FILES)
	$(CXX) -o $@ $+ -lpthread $(LFLAGS)


clean:
	$(RM) $(GE_RS232_OBJECT_FILES) $(GE_RS232D_OBJECT_FILES) ge-rs232d

install: ge-rs232d
	$(INSTALL) ge-rs232d $(PREFIX)/bin
#	$(INSTALL) ge-rs232d.1 $(PREFIX)/share/man/man1

uninstall:
	$(RM) $(PREFIX)/bin/ge-rs232d $(PREFIX)/share/man/man1/ge-rs232d.1

