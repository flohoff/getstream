CC=gcc
CFLAGS=-Wall -I. 

ifdef OPT
CFLAGS+=-O2
else
CFLAGS+=-O0 -g
endif

LDFLAGS=-lpthread

MDEFS := $(shell sh Makefile.defs.sh >Makefile.defs)
include Makefile.defs

OBJ-getstream=getstream.o fe.o crc32.o \
	libhttp.o libconf.o config.o util.o logging.o \
	stream.o input.o \
	output.o output_http.o output_udp.o output_pipe.o output_rtp.o \
	dmx.o dvr.o \
	pat.o pmt.o psi.o sdt.o \
	simplebuffer.o sap.o \
	socket.o

OBJ-tsdecode=tsdecode.o psi.o crc32.o 

all: getstream tsdecode

tsdecode: $(OBJ-tsdecode)
	gcc -o $@ $+ $(LDFLAGS)

getstream: $(OBJ-getstream)
	gcc -o $@ $+ $(LDFLAGS) 

clean:
	-rm -f $(OBJ-getstream) $(OBJ-tsdecode)
	-rm -f getstream tsdecode 
	-rm -f core vgcore.pid* core.* gmon.out
	-rm -f Makefile.defs

distclean: clean
	-rm -rf CVS .cvsignore .git .gitignore
