
#include <sys/types.h>
#include <sys/socket.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "getstream.h"
#include "simplebuffer.h"
#include "output.h"
#include "socket.h"

#define UDP_MAX_TS	((1500-40)/TS_PACKET_SIZE)

int output_init_udp(struct output_s *o) {
	struct sockaddr_in	rsin;

	memset(&rsin, 0, sizeof(struct sockaddr_in));

	o->buffer=sb_init(UDP_MAX_TS, TS_PACKET_SIZE, 0);
	if (o->buffer == NULL)
		return 0;

	o->sockfd=socket_open(NULL, 0);
	socket_set_nonblock(o->sockfd);
	socket_connect(o->sockfd, o->remoteaddr, o->remoteport);

	/* Join Multicast group if its a multicast destination */
	socket_join_multicast(o->sockfd, o->remoteaddr);

	/*
	 * Set socket TTL - Be warned - I DoSed a Cisco 5500 RSM by sending
	 * a 60MBit/s stream in 14 Groups all with TTL of 1 and the switch
	 * went to lala land. It seems dropping MCAST traffic is very expensive
	 * in IOS 12.1 and its even dropped in Layer 3 instead of Layer 2 although
	 * nobody expects ICMP "TTL expired" for MCAST traffic
	 */
	if (o->ttl)
		socket_set_ttl(o->sockfd, o->ttl);

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

