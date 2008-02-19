
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include "output.h"
#include "simplebuffer.h"

void socket_close(int sock) {
	close(sock);
}

int socket_open(char *laddr, int port) {
	struct sockaddr_in	lsin;
	int			sock;

	memset(&lsin, 0, sizeof(struct sockaddr_in));

	sock=socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock<0)
		return sock;

	lsin.sin_family=AF_INET;
	lsin.sin_addr.s_addr=INADDR_ANY;
	if (laddr)
		inet_aton(laddr, &lsin.sin_addr);
	lsin.sin_port=htons(port);

	if (bind(sock, (struct sockaddr *) &lsin,
			sizeof(struct sockaddr_in)) != 0) {
		close(sock);
		return -1;
	}

	return sock;
}

int socket_set_nonblock(int sock) {
	unsigned int	flags;

	flags=fcntl(sock, F_GETFL);
	return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

int socket_set_ttl(int sock, int ttl) {
	if (ttl)
		return setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	return 0;
}

/* Join the socket on a multicats group e.g. tell the kernel 
 * to send out IGMP join messages ...
 *
 * Returns 0 on success and != 0 in failure
 *
 */
int socket_join_multicast(int sock, char *addr) {
	struct ip_mreq		mreq;

	memset(&mreq, 0, sizeof(struct ip_mreq));

	/* Its not an ip address ? */
	if (!inet_aton(addr, &mreq.imr_multiaddr))
		return -1;

	if (!IN_MULTICAST(ntohl(mreq.imr_multiaddr.s_addr)))
		return 0;

	mreq.imr_interface.s_addr=INADDR_ANY;

	return setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
}

int socket_connect(int sock, char *addr, int port) {
	struct sockaddr_in	rsin;

	memset(&rsin, 0, sizeof(struct sockaddr_in));

	/* Create remote end sockaddr_in */
	rsin.sin_family=AF_INET;
	rsin.sin_port=htons(port);
	rsin.sin_addr.s_addr=INADDR_ANY;

	if (addr)
		inet_aton(addr, &rsin.sin_addr);

	return connect(sock, (struct sockaddr *) &rsin, sizeof(struct sockaddr_in));
}
