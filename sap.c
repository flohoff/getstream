
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <stdint.h>
#include <event.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "getstream.h"
#include "sap.h"
#include "socket.h"

/*
 * Implement Mini-SAP Service
 *
 * RFC 2974 (Session Announcement Protocol)
 * http://www.ietf.org/rfc/rfc2974.txt
 * RFC 2327 (SDP: Session Description Protocol)
 * http://www.ietf.org/rfc/rfc2327.txt
 *
 */

/*
 RFC 2327

        v=0
        o=mhandley 2890844526 2890842807 IN IP4 126.16.64.4

o=<username> <session id> <version> <network type> <address type> <address>

        s=SDP Seminar
        i=A Seminar on the session description protocol
        u=http://www.cs.ucl.ac.uk/staff/M.Handley/sdp.03.ps
        e=mjh@isi.edu (Mark Handley)
        c=IN IP4 224.2.17.12/127

c=<network type> <address type> <connection address>

        t=2873397496 2873404696
        a=recvonly
        m=audio 49170 RTP/AVP 0
        m=video 51372 RTP/AVP 31
        m=application 32416 udp wb

m=<media> <port> <transport> <fmt list>

        a=orient:portrait
*/

static void sap_init_timer_single(struct sap_s *sap);
static char	sappkt[SAP_MAX_SIZE];
static int	sid=1;			/* MSG Identifier - Unique for each session */

#define SAP_VERSION		1
#define SAP_VERSION_SHIFT	5

static void sap_send(int fd, short event, void *arg) {
	struct sap_s		*sap=arg;
	char			*sp;

	/* Clear Packet */
	memset(&sappkt, 0, SAP_MAX_SIZE);

	sappkt[0]=SAP_VERSION<<SAP_VERSION_SHIFT;	/* Version + Bitfield */
	sappkt[1]=0x0;					/* Auth len */
	sappkt[2]=sap->sid>>8&0xff;			/* Unique session ID */
	sappkt[3]=sap->sid&0xff;

	/*
	 * FIXME - This would need to be the originating
	 * not the destination address
	 */
	sappkt[4]=sap->addr&0xff;
	sappkt[5]=sap->addr>>8&0xff;
	sappkt[6]=sap->addr>>16&0xff;
	sappkt[7]=sap->addr>>24&0xff;

	sp=sappkt+8;

	sp+=sprintf(sp, "v=0\r\n");
	sp+=sprintf(sp, "o=%s\r\n", sap->odata);
	if (sap->output->stream->name)
		sp+=sprintf(sp, "s=%s\r\n", sap->output->stream->name);
	sp+=sprintf(sp, "t=0 0\r\n");
	sp+=sprintf(sp, "a=tool:getstream\r\n");
	sp+=sprintf(sp, "a=type:broadcast\r\n");
	sp+=sprintf(sp, "m=%s\r\n", sap->mdata);
	sp+=sprintf(sp, "c=%s\r\n", sap->cdata);

	if (sap->playgroup) {
		sp+=sprintf(sp, "a=x-plgroup:%s\r\n", sap->playgroup);
	}

	send(sap->fd, sappkt, sp-sappkt, MSG_DONTWAIT);

	sap_init_timer_single(sap);
}

static void sap_init_timer_single(struct sap_s *sap) {
	static struct timeval	tv;

	/*
	 * Create timer to send out SAPs regularly
	 */

	tv.tv_usec=0;
	tv.tv_sec=1;

	evtimer_set(&sap->event, &sap_send, sap);
	evtimer_add(&sap->event, &tv);
}

static void sap_init_socket_single(struct sap_s *sap) {
	char		*maddr;
	int		port;

	sap->fd=socket_open(NULL, 0);

	if (sap->fd == -1) {
		fprintf(stderr, "Unable to open SAP socket\n");
		exit(-1);
	}

	/* Last resort Multicast config */
	maddr=SAP_V4_GLOBAL_ADDRESS;
	port=SAP_PORT;

	/* get SAP remote address from scope or group */
	if (sap->scope) {
		/* Most simple way - We have a scope */
		if (strcasecmp(sap->scope, "global") == 0) {
			maddr=SAP_V4_GLOBAL_ADDRESS;
		} else if(strcasecmp(sap->scope, "org") == 0) {
			maddr=SAP_V4_ORG_ADDRESS;
		} else if(strcasecmp(sap->scope, "local") == 0) {
			maddr=SAP_V4_LOCAL_ADDRESS;
		} else {
			maddr=SAP_V4_LINK_ADDRESS;
		}
		port=SAP_PORT;
	} else if(sap->group) {
		maddr=sap->group;
		port=sap->port;
	}

	socket_join_multicast(sap->fd, maddr);
	socket_connect(sap->fd, maddr, port);
	socket_set_ttl(sap->fd, sap->ttl);
}

/* Create SDP Connection Data as per RFC2327 */
static char *sap_init_cdata(struct sap_s *sap) {
	char		cdata[128];

	/* mcast c=IN IP4 ipaddr/ttl */
	/* ucast c=IN IP4 ipaddr */

	/* Ignore the ucast case for UDP and RTP as it does not make
	 * sense to announce ucast UDP/RTP
	 */
	switch(sap->output->type) {
		case(OTYPE_UDP):
		case(OTYPE_RTP):
			sprintf(cdata, "IN IP4 %s/%d",
				sap->output->remoteaddr,
				sap->output->ttl);
			break;
		case(OTYPE_RTCP):
			if (sap->output->localaddr) {
				sprintf(cdata, "IN IP4 %s/%d",
					sap->output->localaddr,
					sap->output->ttl);
			} else {
				char	hname[80];

				gethostname(hname, sizeof(hname));
				sprintf(cdata, "IN IP4 %s/%d",
					hname,
					sap->output->ttl);
			}
			break;
		default:
			fprintf(stderr, "Unknown stream output type in %s", __FUNCTION__);
			exit(-1);
	}

	return strdup(cdata);
}

/* Create SDP Media Announcement as per RFC2327 */
static char *sap_init_mdata(struct sap_s *sap) {
	char		mdata[128];

	/* m=audio/video ipport udp 33 */
	/* m=audio/video ipport rtp/avp 31 */

	switch(sap->output->type) {
		case(OTYPE_UDP):
			sprintf(mdata, "video %d udp 33",
				sap->output->remoteport);
			break;
		case(OTYPE_RTP):
			sprintf(mdata, "video %d RTP/AVP 33",
				sap->output->remoteport);
			break;
		case(OTYPE_RTCP):
			sprintf(mdata, "video %d RTP/AVP 33",
				sap->output->rtpport);
			break;
	}

	return strdup(mdata);
}

/* Create SDP Origin as per RFC2327 */
static char *sap_init_odata(struct sap_s *sap) {
	char	odata[128];

	switch(sap->output->type) {
		case(OTYPE_UDP):
		case(OTYPE_RTP):
			sprintf(odata, "- %d %lu IN IP4 %s",
				sap->sid,
				time(NULL),
				sap->output->remoteaddr);
			break;
		case(OTYPE_RTCP):
			if (sap->output->localaddr) {
				sprintf(odata, "IN IP4 %s",
					sap->output->localaddr);
			} else {
				char	hname[80];

				gethostname(hname, sizeof(hname));
				sprintf(odata, "- %d %lu IN IP4 %s",
						sap->sid,
						time(NULL),
						hname);
			}
			break;
	}
	return strdup(odata);
}


int sap_init(struct sap_s *sap) {

	/* Copy a Session ID into the SAP structure. This needs to be unique
	 * for a SAP sender and needs to be changed in case the SAP announcement
	 * changes
	 */
	sap->sid=sid++;

	/* Open Socket */
	sap_init_socket_single(sap);

	/* Create Stream Connection Data for SDP */
	sap->cdata=sap_init_cdata(sap);
	/* Create Media Announcement Data for SDP */
	sap->mdata=sap_init_mdata(sap);
	/* Create Origin Data for SDP */
	sap->odata=sap_init_odata(sap);

	/* Start timer */
	sap_init_timer_single(sap);
	return 1;
}



