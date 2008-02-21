
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "output.h"
#include "simplebuffer.h"
#include "socket.h"


#if 0


static inline void sout_send_tsp_rtp(struct stream_s *s, uint8_t *tsp) {
}

static void sout_sock_read(int fd, short event, void *arg) {
	struct stream_s		*s=arg;
	ssize_t			size;
	struct sockaddr_in	sin;
	socklen_t		sinlen=sizeof(struct sockaddr_in);

	size=recvfrom(fd, s->ctlbuf, MAX_CTL_MSG_SIZE,
			0, (struct sockaddr *) &sin, &sinlen);

	sout_parse_rtcp(s, s->ctlbuf, size, &sin);
}

static int sout_init_socket(struct stream_s *s) {
	struct sockaddr_in	sin;

	memset(&sin, 0, sizeof(struct sockaddr_in));

	s->sockfd=socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	sin.sin_port=htons(s->port);
	sin.sin_family=AF_INET;
	sin.sin_addr.s_addr=s->groupinaddr.s_addr;

	if (s->dist == STREAM_DIST_LISTEN) {

		/* Open control aka RTCP socket. I reverse engineered
		 * that this is group socket + 1 */

		s->ctlfd=socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
		sin.sin_port=htons(s->port+1);
		bind(s->ctlfd, (struct sockaddr *) &sin,
				sizeof(struct sockaddr_in));

		/* Add callback for packet receiption on control socket e.g. RTCP */
		event_set(&s->ctlevent, s->ctlfd, EV_READ|EV_PERSIST,
				sout_sock_read, s);
		event_add(&s->ctlevent, NULL);
	} else {
		/* MCAST - Set destination */
		connect(s->sockfd, (struct sockaddr *) &sin, sizeof(struct sockaddr_in));

		if (s->dist == STREAM_DIST_MCAST) {

			/*
			 * Set socket TTL - Be warned - I DoSed a Cisco 5500 RSM by sending
			 * a 60MBit/s stream in 14 Groups all with TTL of 1 and the switch
			 * went to lala land. It seems dropping MCAST traffic is very expensive
			 * in IOS 12.1 land and its even dropped in Layer 3 instead of Layer 2 although
			 * nobody expects ICMP "TTL expired" for MCAST traffic
			 *
			 */

			setsockopt(s->sockfd, IPPROTO_IP, IP_MULTICAST_TTL,
						&s->ttl, sizeof(s->ttl));
		}
	}

	return 1;
}

#endif

int output_rtp_new_receiver(struct output_s *o,
		char *addr, int port, uint32_t ssrc) {
	struct rtp_receiver_s	*r;

	r=calloc(1, sizeof(struct rtp_receiver_s));

	/* Copy address and port */
	r->addr=strdup(addr);
	r->port=port;
	r->ssrc=ssrc;
	r->lastrr=time(NULL);

	/* Create sockaddr_in struct for later sendmsg */
	r->sin.sin_family=AF_INET;
	inet_aton(r->addr, &r->sin.sin_addr);
	r->sin.sin_port=htons(r->port);

	r->sinlen=sizeof(struct sockaddr_in);

	/* Prepend receiver to receiver list */
	r->next=o->rtpreceiver;
	o->rtpreceiver=r;

	/* We want to receive packets */
	o->receiver++;

	return 0;
}

#if 0

static struct rtp_receiver_s *output_rtp_find_rtpr(struct output_s *o, uint32_t ssrc) {
	struct rtp_receiver_s	*r;

	for(r=o->rtpreceiver;r;r=r->next)
		if (r->ssrc == ssrc)
			return r;

	return NULL;
}

static void output_rtp_free_rtpr(struct output_s *o, uint32_t ssrc) {
	struct rtp_receiver_s	*r, *lr;

	for(r=o->rtpreceiver,lr=NULL;r;r=r->next) {
		if (r->ssrc == ssrc) {
			if (lr)
				lr->next=r->next;
			free(r);
			return;
		}

		lr=r;
	}
}

static void output_rtp_parse_rtcp(struct output_s *o, uint8_t *b,
			int len, struct sockaddr_in *sin) {
	struct rtp_receiver_s	*r;
	uint32_t		ssrc;

	logwrite(LOG_DEBUG, "streamrtp: got rtcp packet version %d\n", RTCP_VERSION(b));

	/* Version 2 ? */
	if (RTCP_VERSION(b) != 2)
		return;

	/* Get SSRC from RTCP packet */
	ssrc=b[3]<<24 | b[4]<<16 | b[5]<<8 | b[6];

	switch(RTCP_PT(b)) {
		case(RTP_PT_RR):
			logwrite(LOG_DEBUG, "streamrtp: Got Receiver Report size %d from %s ssrc %08x\n",
						len, inet_ntoa(sin->sin_addr), ssrc);

			r=output_rtp_find_rtpr(o, ssrc);

			if (!r) {
				logwrite(LOG_INFO, "streamrtp: Createing new RTP Receiver %08x\n", ssrc);

				output_rtp_new_receiver(o,
					inet_ntoa(sin->sin_addr),
					ntohs(sin->sin_port)-1,
					ssrc);
			} else {
				/* Store last RR timestamp */
				r->lastrr=time(NULL);
			}
			break;

		case(RTP_PT_BYE):
			logwrite(LOG_INFO, "streamrtp: Got Bye size %d from %s ssrc %08x\n",
				len, inet_ntoa(sin->sin_addr), ssrc);

			/* Find receiver struct */
			output_rtp_free_rtpr(o, ssrc);
			break;
	}
}

static void output_rtp_read_rtcp(int fd, short event, void *arg) {
	struct output_s		*o=arg;
	ssize_t			size;
	struct sockaddr_in	sin;
	socklen_t		sinlen=sizeof(struct sockaddr_in);

	size=recvfrom(fd, o->rtcpbuffer, RTCP_BUFFER_SIZE,
			0, (struct sockaddr *) &sin, &sinlen);

	logwrite(LOG_DEBUG, "streamrtp: got packet size %d\n", size);

	output_rtp_parse_rtcp(o, o->rtcpbuffer, size, &sin);
}

static void output_init_rtp_rtcp(struct output_s *o) {
	struct sockaddr_in	sin;

	/* Allocate the RTCP incoming packet buffer */
	o->rtcpbuffer=malloc(RTCP_BUFFER_SIZE);

	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family=AF_INET;
	sin.sin_addr.s_addr=INADDR_ANY;
	sin.sin_port=htons(o->rtcpport);

	/* Create a socket and bind */
	o->rtcpfd=socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	bind(o->rtcpfd, (struct sockaddr *) &sin, sizeof(struct sockaddr_in));

	/* Install rtcp callback */
	event_set(&o->rtcpevent, o->rtcpfd,
			EV_READ|EV_PERSIST, output_rtp_read_rtcp, o);
	event_add(&o->rtcpevent, NULL);
}
#endif

int output_init_rtp(struct output_s *o) {
	o->sockfd=socket_open(o->localaddr, 0);

	if (o->sockfd < 0)
		return -1;

	socket_set_nonblock(o->sockfd);
	socket_set_ttl(o->sockfd, o->ttl);

	/* Join multicast group if its a multicast address */
	socket_join_multicast(o->sockfd, o->remoteaddr),

	/* Do you have a better idea ? */
	o->rtpssrc=(uint32_t) random();
	o->buffer=sb_init(RTP_MAX_TS, TS_PACKET_SIZE, RTP_HEADROOM);

	output_rtp_new_receiver(o,
		o->remoteaddr, o->remoteport, 0);

	return 0;
}

void output_send_rtp(struct output_s *o, uint8_t *tsp) {

	sb_add_atoms(o->buffer, tsp, 1);

	/* check whether another packet would fit ? */
	if (!sb_free_atoms(o->buffer)) {
		struct rtp_receiver_s	*r;
		struct timeval		tv;
		long			msec;
		uint8_t			*b=sb_bufptr(o->buffer);

		gettimeofday(&tv, (struct timezone *) NULL);
		msec=(tv.tv_sec%1000000)*1000 + tv.tv_usec/1000;

		b[RTP_VERSION_OFF]	= 0x80;
		b[RTP_PT_OFF]		= RTP_PT_MP2T;	/* RFC 2250 */

		b[RTP_SEQ_OFF]		= o->rtpseq >> 8 & 0xff;
		b[RTP_SEQ_OFF+1]	= o->rtpseq & 0xff;

		b[RTP_TSTAMP_OFF]	= msec>>24 & 0xff;
		b[RTP_TSTAMP_OFF+1]	= msec>>16 & 0xff;
		b[RTP_TSTAMP_OFF+2]	= msec>>8 & 0xff;
		b[RTP_TSTAMP_OFF+3]	= msec & 0xff;

		b[RTP_SSRC_OFF]		= o->rtpssrc>>24 & 0xff;
		b[RTP_SSRC_OFF+1]	= o->rtpssrc>>16 & 0xff;
		b[RTP_SSRC_OFF+2]	= o->rtpssrc>>8 & 0xff;
		b[RTP_SSRC_OFF+3]	= o->rtpssrc & 0xff;

		for(r=o->rtpreceiver;r;r=r->next) {
			int	len;
			len=sendto(o->sockfd, sb_bufptr(o->buffer), sb_buflen(o->buffer),
					MSG_DONTWAIT,
					(struct sockaddr *) &r->sin, r->sinlen);
		}

		sb_zap(o->buffer);
		o->rtpseq++;
	}
}
