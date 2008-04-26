CC=gcc
CFLAGS=-O0 -g -Wall  -I. -I/usr/include/glib-2.0/ -I/usr/lib/glib-2.0/include/
LDFLAGS=-levent -lglib-2.0 -lpthread
OBJ-getstream=getstream.o fe.o crc32.o \
	libhttp.o libconf.o config.o util.o logging.o \
	stream.o input.o \
	output.o output_http.o output_udp.o output_pipe.o output_rtp.o \
	dmx.o dvr.o \
	pat.o pmt.o psi.o \
	simplebuffer.o sap.o \
	socket.o

OBJ-tsdecode=tsdecode.o psi.o crc32.o 

all: getstream tsdecode

tsdecode: $(OBJ-tsdecode)
	gcc $(LDFLAGS) -o $@ $+

getstream: $(OBJ-getstream)
	gcc $(LDFLAGS) -o $@ $+

clean:
	-rm -f $(OBJ-getstream) $(OBJ-tsdecode)
	-rm -f getstream tsdecode 
	-rm -f core vgcore.pid* core.* gmon.out

distclean: clean
	-rm -rf CVS .cvsignore
