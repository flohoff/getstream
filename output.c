
#include "output.h"
#include "getstream.h"
#include "sap.h"

void output_send(struct output_s *o, uint8_t *tsp) {

	/* Does this stream output have receiver */
	if (o->receiver == 0)
		return;

	switch(o->type) {
		case(OTYPE_UDP):
			output_send_udp(o, tsp);
			break;
		case(OTYPE_RTP):
		case(OTYPE_RTCP):
			output_send_rtp(o, tsp);
			break;
		case(OTYPE_HTTP):
			output_send_http(o, tsp);
			break;
		case(OTYPE_PIPE):
			output_send_pipe(o, tsp);
			break;
	}
}

int output_init(struct output_s *o) {
	/* Initialize all stream outputs for this stream */
	switch(o->type) {
		case(OTYPE_HTTP):
			output_init_http(o);
			break;
		case(OTYPE_UDP):
			output_init_udp(o);
			break;
		case(OTYPE_RTP):
		case(OTYPE_RTCP):
			output_init_rtp(o);
			break;
		case(OTYPE_PIPE):
			output_init_pipe(o);
			break;
	}

	if (o->sap)
		sap_init(o->sap);

	return 1;
}

