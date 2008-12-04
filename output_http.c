
#include <sys/time.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <glib/glist.h>

#include "output.h"
#include "simplebuffer.h"
#include "libhttp.h"

extern struct http_server	*hserver;

static void output_remove_receiver(struct http_receiver_s *hr) {
	struct output_s		*o=hr->output;
	struct http_connection	*hc=hr->hc;

	o->http_receiver=g_list_remove(o->http_receiver, hr);
	o->receiver--;

	logwrite(LOG_INFO, "stream_http: dropping connection to %s for %s",
				inet_ntoa(hc->sin.sin_addr),
				hc->url);

	http_drop_connection(hc);
}

static int output_cb_http(struct http_connection *hc, int cbtype, void *arg) {

	switch(cbtype) {
		case(HCB_QUERY): {
			struct http_receiver_s	*hr;
			struct output_s		*o=arg;	/* HCB_QUERY returns http_url args */

			hr=calloc(1, sizeof(struct http_receiver_s));
			hr->hc=hc;
			hr->output=o;

			/* Put into stream output list */
			o->http_receiver=g_list_append(o->http_receiver, hr);
			o->receiver++;

			/* Store http_receiver into http_connection structure */
			hc->arg=hr;

			/* Return head */
			http_header_start(hc, "200 OK", "application/octet-stream");
			http_header_nocache(hc);
			http_header_clength(hc, -1);
			http_header_end(hc);

			logwrite(LOG_INFO, "stream_http: connection from %s for %s",
						inet_ntoa(hc->sin.sin_addr),
						hc->url);
			break;
		}
		case(HCB_ERROR): {
			output_remove_receiver(hc->arg);
			break;
		}
	}
	return 1;
}

#define HTTP_MAX_TS	(20000/TS_PACKET_SIZE)

int output_init_http(struct output_s *o) {

	o->buffer=sb_init(HTTP_MAX_TS, TS_PACKET_SIZE, 0);
	if (o->buffer == NULL)
		return 0;

	o->hurl=calloc(2, sizeof(struct http_url));

	o->hurl->url=o->url;
	o->hurl->cb=output_cb_http;
	o->hurl->arg=(void *) o;

	http_register_url(hserver, o->hurl);

	return 0;
}

#define HTTP_MAX_QUEUED		(200*1024)
#define HTTP_MAX_OVERFLOW	20

void output_send_http_one(gpointer data, gpointer user_data) {
	struct http_receiver_s	*hr=data;
	struct output_s		*o=user_data;

	/*
	 * Check how many bytes we already have queued on this
	 * HTTP connection. Users might connect from low bandwidth
	 * links not beeing able to transmit the full feed. We must
	 * avoid consuming all memory.
	 *
	 * If the situation persists too long we drop the connection
	 *
	 */
	if (http_get_queue(hr->hc) > HTTP_MAX_QUEUED) {
		hr->overflow++;
		if (hr->overflow > HTTP_MAX_OVERFLOW)
			output_remove_receiver(hr);
		return;
	}
	hr->overflow=0;

	/*
	 * We cant reuse evbuffer as they are empty after
	 * passing the the buffevent layer - Thus we recreate
	 * the buffer every time we send it out. This involes
	 * a little more memcpy as really necessary but for now
	 * its enough
	 *
	 */
	http_return_stream(hr->hc, sb_bufptr(o->buffer), sb_buflen(o->buffer));
}

void output_send_http(struct output_s *o, uint8_t *tsp) {

	sb_add_atoms(o->buffer, tsp, 1);

	if (!sb_free_atoms(o->buffer)) {
		/*
		 * If the output buffer is full - loop on all http sesssions
		 * and send out the buffer as a http chunk
		 */
		g_list_foreach(o->http_receiver,
				output_send_http_one, (gpointer) o);

		sb_zap(o->buffer);
	}
}
