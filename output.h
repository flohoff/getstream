#ifndef STREAM_H
#define STREAM_H

#include "getstream.h"
#include "libhttp.h"

#include <glib/glist.h>

#include <event.h>

#define RTCP_BUFFER_SIZE	4096

#define RTCP_VERSION_OFF	0
#define RTCP_VERSION_SHIFT	6
#define RTCP_PT_OFF		1
#define	RTCP_VERSION(x)		(x[RTCP_VERSION_OFF]>>RTCP_VERSION_SHIFT)
#define RTCP_PT(x)		(x[RTCP_PT_OFF])

#define RTP_PT_H261		31		/* RFC2032 */
#define RTP_PT_MP2T		33		/* RFC2250 */
#define RTP_PT_RR		201
#define RTP_PT_BYE		203

#define RTP_PT_OFF		1
#define RTP_VERSION_OFF		0
#define RTP_SEQ_OFF		2
#define RTP_TSTAMP_OFF		4
#define RTP_SSRC_OFF		8

#define RTP_MAX_PAYLOAD		1000
#define RTP_MAX_TS		(RTP_MAX_PAYLOAD/TS_PACKET_SIZE)
#define RTP_HEADROOM		12


enum {
	OTYPE_UDP,
	OTYPE_RTP,
	OTYPE_RTCP,
	OTYPE_HTTP,
	OTYPE_PIPE
};

#if 0
struct stream_out_rtp_s {
	/* RTCP informations */
	int			rtcpfd;
	struct event		rtcpevent;
	char			*rtcpbuf;
	struct sockaddr		*rtcpsockaddr;
	int			rtcpsockaddrlen;

	/* RTP Informations */
	int			rtpfd;
	struct addrspec		local,
				remote;

	struct rtp_receiver_s	*rcvr;


	int			ttl;

	uint8_t			*buffer;
	int			buffervalid;
};
#endif

struct http_receiver_s {
	struct http_receiver_s	*next;
	struct http_connection	*hc;
	struct output_s		*output;
	int			overflow;
};

struct rtp_receiver_s {
	struct rtp_receiver_s	*next;
	char			*addr;
	int			port;
	struct sockaddr_in	sin;
	int			sinlen;
	time_t			lastrr;
	uint32_t		ssrc;
};

struct output_s	{
	/* Config elements */
	struct output_s		*next;
	int			type;

	/* Simple Buffer */
	void			*buffer;

	/* UDP & RTP - MCast or UCast */
	char			*remoteaddr;
	int			remoteport;
	int			ttl;

	/* RTCP or HTTP local port or local address */
	char			*localaddr;

	struct sap_s		*sap;

	/* */
	//struct channel_s	*channel;
	struct stream_s		*stream;
	int			receiver;		/* No of receivers */
	int			sockfd;

	/* RTP/RTCP */
	uint8_t			*rtcpbuffer;
	struct rtp_receiver_s	*rtpreceiver;
	int			rtcpfd;
	uint16_t		rtpseq;
	uint32_t		rtpssrc;
	int			rtpport,
				rtcpport;
	struct event		rtcpevent;

	/* HTTP */
	char			*url;
	GList			*http_receiver;
	struct http_url		*hurl;

	/* PIPE */
	struct {
		char			*filename;
		int			fd;
		time_t			last;
		struct event		event;
	} pipe;
};

int output_init(struct output_s *channel);
int output_init_udp(struct output_s *o);
int output_init_rtp(struct output_s *o);
int output_init_http(struct output_s *o);
int output_init_pipe(struct output_s *o);

void output_send(struct output_s *c, uint8_t *tsp);
void output_send_udp(struct output_s *o, uint8_t *tsp);
void output_send_rtp(struct output_s *o, uint8_t *tsp);
void output_send_http(struct output_s *o, uint8_t *tsp);
void output_send_pipe(struct output_s *o, uint8_t *tsp);

#endif
