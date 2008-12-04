#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>

#include "getstream.h"

int addr_is_mcast(char *addr) {
	struct in_addr	iaddr;
	inet_aton(addr, &iaddr);
	return IN_MULTICAST(iaddr.s_addr);
}

void dump_hex(int level, const char *prefix, uint8_t *buf, int size) {
	int		i;
	unsigned char	ch;
	char		sascii[17];
	char		linebuffer[16*4+1];

	/* Speedup */
	if (level > loglevel)
		return;

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
			logwrite(level, "%s %s  %s", prefix, linebuffer, sascii);
	}

	/* i++ after loop */
	if (i%16 != 0) {
		for(;i%16 != 0;i++) {
			sprintf(&linebuffer[(i%16)*3], "   ");
			sascii[i%16]=' ';
		}

		logwrite(level, "%s %s  %s", prefix, linebuffer, sascii);
	}
}

void ts_packet_decode(uint8_t *ts) {
	logwrite(LOG_DEBUG, "ts_packet_decode\n");
	dump_hex(LOG_DEBUG, "pdecode:", ts, TS_PACKET_SIZE);
	logwrite(LOG_DEBUG, "\tsync: %02x\n", ts[0]);
	logwrite(LOG_DEBUG, "\ttransport_error_indicator: %d\n", ts[1]&0x80 ? 1 : 0);
	logwrite(LOG_DEBUG, "\tpayload_unit_start_indicator: %d\n", ts[1]&0x40 ? 1 : 0);
	logwrite(LOG_DEBUG, "\ttransport_priority: %d\n", ts[1]&0x20 ? 1 : 0);
	logwrite(LOG_DEBUG, "\tpid: %d\n", (ts[1]<<8|ts[2])&0x1fff);
	logwrite(LOG_DEBUG, "\ttransport_scrambling_control: %d\n", (ts[3]>>6) & 0x3);
	logwrite(LOG_DEBUG, "\tadaption_field_control: %d\n", (ts[3]>>4) & 0x3);
	logwrite(LOG_DEBUG, "\tcontinuity_counter: %d\n", ts[3]&0xf);
}

