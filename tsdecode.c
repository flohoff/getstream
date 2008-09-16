#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "getstream.h"
#include "psi.h"

#define TS_PID_OFF1		1
#define TS_PID_OFF2		2
#define TS_CC_OFF		3
#define TS_CC_MASK		0xf
#define TS_AFC_OFF		3
#define TS_AFC_MASK		0x30
#define TS_AFC_SHIFT		4
#define TS_AFC_LEN		4
#define TS_HEAD_MIN		4

void _dump_hex(char *prefix, uint8_t *buf, int size) {
	int		i;
	unsigned char	ch;
	char		sascii[17];
	char		linebuffer[16*4+1];

	sascii[16]=0x0;

	for(i=0;i<size;i++) {
		ch=buf[i];
		if (i%16 == 0) {
			sprintf(linebuffer, "%04x ", i);
		}
		sprintf(&linebuffer[(i%16)*3], "%02x ", ch);
		if (ch >= ' ' && ch <= '}')
			sascii[i%16]=ch;
		else
			sascii[i%16]='.';

		if (i%16 == 15)
			printf("%s %s  %s\n", prefix, linebuffer, sascii);
	}

	/* i++ after loop */
	if (i%16 != 0) {
		for(;i%16 != 0;i++) {
			sprintf(&linebuffer[(i%16)*3], "   ");
			sascii[i%16]=' ';
		}

		printf("%s %s  %s\n", prefix, linebuffer, sascii);
	}
}


static int	pktno;

void decodebits(unsigned int bits, unsigned int mask, 
			unsigned int len, char *prefix, char *name) {
	char	line[128];
	int	val=0, i, j=0;

	for(i=len-1;i>=0;i--) {
		if (mask & (1<<i)) {
			line[j++]=(bits & (1<<i)) ? '1' : '0';
			val=val<<1|((bits & (1<<i)) ? 1 : 0);
		} else {
			line[j++]='.';
		}
	}

	line[j++]=0x0;

	printf("%s%s %s (%d)\n", prefix, line, name, val);

}

static void decode_tspkt(uint8_t *ts) {
	unsigned int	bits;
	printf("Packet: %u\n", pktno);
	_dump_hex("  ", ts, TS_PACKET_SIZE);

	bits=ts[1]<<8|ts[2];
	printf("  PID: 0x%04x\n", bits);
	decodebits(bits, 0x8000, 16, "    ", "transport error indicator");
	decodebits(bits, 0x4000, 16, "    ", "payload_unit_start_indicator");
	decodebits(bits, 0x2000, 16, "    ", "transport priority");
	decodebits(bits, 0x1fff, 16, "    ", "PID");

	bits=ts[3];
	printf("  AFC: 0x%02x\n", bits);
	decodebits(bits, 0xc0, 8, "    ", "transport scrambling control");
	decodebits(bits, 0x30, 8, "    ", "adaption field control");
	decodebits(bits, 0x0f, 8, "    ", "continuity counter");

	if (ts_has_af(ts)) {
		printf("  TS Packet has Adaption field (%d)\n", ts_af_len(ts));
	}
}

#define ts_sync(ts) (ts[0] == 0x47)

static struct psisec_s		patsec;
static struct psi_s		pat;

void tsd_pat_section_dump(struct psisec_s *s) {
	uint8_t		*pat=s->data;
	unsigned int	bits;
	unsigned int	off;

	decodebits(pat[0], 0xff, 8, "      ", "table_id");

	bits=pat[1]<<8|pat[2];
	decodebits(bits, 0x8000, 16, "      ", "section syntax indicator");
	decodebits(bits, 0x4000, 16, "      ", "0");
	decodebits(bits, 0x3000, 16, "      ", "reserved");
	decodebits(bits, 0x0fff, 16, "      ", "section length");

	bits=pat[3]<<8|pat[4];
	printf("      0x%04x transport stream id\n", bits);

	bits=pat[5];
	decodebits(bits, 0xc0, 8, "      ", "reserved");
	decodebits(bits, 0x3e, 8, "      ", "version");
	decodebits(bits, 0x01, 8, "      ", "current next indicator");

	printf("      0x%02x section number\n", pat[PAT_SECTION_OFF]);
	printf("      0x%02x last section number\n", pat[PAT_LAST_SECTION_OFF]);

	off=PAT_HDR_LEN;

	printf("      Program_number program_map_pid\n");

	while(off < _psi_len(pat)-4) {
		uint16_t pnr;
		uint16_t pid;

		pnr=pat[off]<<8|pat[off+1];
		pid=(pat[off+2]<<8|pat[off+3])&PID_MASK;
		printf("                %04x %04x\n", pnr, pid);

		off+=4;
	}
}

void tsd_pat(uint8_t *ts, uint16_t pid) {
	int		off=0;
	while(off < TS_PACKET_SIZE) {
		off=psi_reassemble(&patsec, ts, off);

		if (off < 0)
			break;

		printf("%u pid %04x new pat section complete\n", pktno, pid);
		tsd_pat_section_dump(&patsec);
		psi_update_table(&pat, &patsec);
	}
}

void tsd_packetin(uint8_t *ts) {
	uint16_t	pid;
	if (!ts_sync(ts)) {
		fprintf(stderr, "%06d Missing sync\n", pktno);
		return;
	}

	if (ts_tei(ts))
		fprintf(stderr, "%06d Packet has set TEI\n", pktno);

	pid=ts_pid(ts);

	switch(pid) {
		case(0):
			tsd_pat(ts, pid);
			break;
	}
}

int main(void ) {
	uint8_t		tsbuf[TS_PACKET_SIZE];
	int		len, valid=0, toread, no;
	fd_set		fdin;

	while(1) {
		toread=TS_PACKET_SIZE-valid;

		FD_ZERO(&fdin);
		FD_SET(fileno(stdin), &fdin);

		no=select(1, &fdin, NULL, NULL, NULL);
		if (!no)
			continue;

		len=read(fileno(stdin), &tsbuf, toread);

		if (len == 0 || len < 0) {
			printf("Aborting - short read %d/%d\n", len, toread);
			exit(0);
		}

		valid+=len;

		if (valid != TS_PACKET_SIZE)
			continue;

		pktno++;
		tsd_packetin(tsbuf);
		valid=0;
	}

}
