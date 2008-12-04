
#include <sys/types.h>
#include <sys/socket.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "getstream.h"
#include "simplebuffer.h"
#include "output.h"

#define UDP_MAX_TS	((1500-40)/TS_PACKET_SIZE)

int output_init_udp(struct output_s *o) {
	struct sockaddr_in	lsin, rsin;

	memset(&lsin, 0, sizeof(struct sockaddr_in));
	memset(&rsin, 0, sizeof(struct sockaddr_in));

	o->buffer=sb_init(UDP_MAX_TS, TS_PACKET_SIZE, 0);
	if (o->buffer == NULL)
		return 0;

	o->sockfd=socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	/* Create local end sockaddr_in */
	lsin.sin_family=AF_INET;
	lsin.sin_port=0;
	lsin.sin_addr.s_addr=INADDR_ANY;
	bind(o->sockfd, (struct sockaddr *) &lsin, sizeof(struct sockaddr_in));

	/* Create remote end sockaddr_in */
	rsin.sin_family=AF_INET;
	rsin.sin_port=htons(o->remoteport);
	inet_aton(o->remoteaddr, &rsin.sin_addr);
	connect(o->sockfd, (struct sockaddr *) &rsin, sizeof(struct sockaddr_in));

	/*
	 * Set socket TTL - Be warned - I DoSed a Cisco 5500 RSM by sending
	 * a 60MBit/s stream in 14 Groups all with TTL of 1 and the switch
	 * went to lala land. It seems dropping MCAST traffic is very expensive
	 * in IOS 12.1 and its even dropped in Layer 3 instead of Layer 2 although
	 * nobody expects ICMP "TTL expired" for MCAST traffic
	 *
	 */
	if (o->ttl) {
		setsockopt(o->sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &o->ttl, sizeof(o->ttl));
	}

	/* We do want to get TSP packets */
	o->receiver=1;

	return 1;
}

void output_send_udp(struct output_s *o, uint8_t *tsp) {
	int	len;

	sb_add_atoms(o->buffer, tsp, 1);

	/* check whether another packet would fit ? */
	if (!sb_free_atoms(o->buffer)) {
		/* Send packet and reset valid counter */
		len=send(o->sockfd, sb_bufptr(o->buffer), sb_buflen(o->buffer), MSG_DONTWAIT);

		if (len != sb_buflen(o->buffer))
			logwrite(LOG_DEBUG, "streamudp: send didnt send all... %d/%d\n",
					len, sb_buflen(o->buffer));

		sb_zap(o->buffer);
	}
}

