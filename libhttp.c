
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <event.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include <glib/glist.h>

#include "libhttp.h"

//#define DEBUG
#ifdef DEBUG
#define Dprintf		printf
#else
#define Dprintf( a... )
#endif

int http_get_cmd(char *cmd) {
	if (strcasecmp("GET", cmd) == 0)
		return HC_CMD_GET;
	if (strcasecmp("POST", cmd) == 0)
		return HC_CMD_POST;
	if (strcasecmp("HEAD", cmd) == 0)
		return HC_CMD_HEAD;

	return HC_CMD_UNKNOWN;
}

/* Split line into string elements seperated by space or tab */
static int split_string_by_space(char *input, char **elem, int max) {
	int	no;
	char	*i=input;

	for(no=0;no<max;) {
		/* Skip space or tab */
		while(*i == ' ' || *i == '\t' || *i == '\r' || *i == '\n')
			*i++=0x0;
		if(*i == 0x0)
			break;
		/* Store pointer */
		elem[no++]=i;
		/* Skip non space */
		while(*i != ' ' && *i != '\t' && *i != 0x0)
			i++;
		/* End of String ? */
		if(*i == 0x0)
			break;
	}

	return no;
}

/*
 * Parse a string like HTTP/1.0 or HTTP/1.1 and return
 * the protocol and version
 *
 *
 * HP_UNKNOWN		- Unknown protocol
 * HP_HTTP		- Seems to be HTTP without/with broken/with unknown version
 * HP_HTTP10		- HTTP/1.0
 * HP_HTTP11		- HTTP/1.1
 *
 */
static int http_parse_proto(char *pstr) {
	char		*pver, *dotptr, *endptr;
	int		major, minor;

	/* Check if we are dealing with HTTP */
	if (strncasecmp(pstr, "HTTP", 4) != 0)
		return HP_UNKNOWN;

	pver=strchr(pstr, '/');
	if (!pver)
		return HP_HTTP;

	major=strtol(++pver, &dotptr, 10);

	/* 1.0 or 1.1 */
	if (*dotptr != '.')
		return HP_HTTP;

	minor=strtol(++dotptr, &endptr, 10);

	/* String should end after [major].[minor] */
	if (*endptr != 0x0)
		return HP_HTTP;

	Dprintf("%s: major %d minor %d\n", __FUNCTION__, major, minor);

	if (major == 1 && minor == 0)
		return HP_HTTP10;
	if (major == 1 && minor == 1)
		return HP_HTTP11;

	return HP_HTTP;
}

/*
 * Read the first line - PUT/GET/POST etc ...
 *
 * GET <url> HTTP/<version>
 *
 */
static int http_parse_command_url(struct http_connection *hc, struct evbuffer *input) {
	char	*p=(char *) EVBUFFER_DATA(input);
	char	*elem[10];
	int	no;

	no=split_string_by_space(p, elem, 10);

	/* Need "CMD URL PROTO/VERSION" which makes it 3 parms */
	if (no != 3)
		return 0;

	if ((hc->cmd=http_get_cmd(elem[0])) == HC_CMD_UNKNOWN)
		return 0;

	hc->url=strdup(elem[1]);
	hc->proto=http_parse_proto(elem[2]);

	if (hc->proto == HP_HTTP11)
		hc->keepalive=1;
	else
		hc->keepalive=0;

	return 1;
}

static int http_parse_attrib(struct http_connection *hc, struct evbuffer *input) {
	char	*p=(char *) EVBUFFER_DATA(input);
	struct	http_attrib	*ha;
	char	*token, *value;

	token=p;

	while(*p != ':' && *p != ' ' && *p != 0x0)
		p++;

	/* Did we find the colon as delimiter ?
	 * If not something went really wrong - terminate the connection
	 */
	if (*p == ':')
		*p++=0x0;
	else
		return 0;

	while(*p == ' ' || *p == '\t')
		p++;

	value=p;

	ha=malloc(sizeof(struct http_attrib));
	ha->token=strdup(token);
	ha->value=strdup(value);

	hc->attrib=g_list_append(hc->attrib, ha);

	Dprintf("%s: %s %s\n", __FUNCTION__, token, value);

	return 1;
}

/*
 * HTTP/1.1 404 Not Found
 * Date: Wed, 17 May 2006 17:33:00 GMT
 * Server: Apache/2.0.54 (Debian GNU/Linux)
 * Content-Length: 310
 * Connection: close
 * Content-Type: text/html; charset=iso-8859-1
 *
 */
static char *http_static_msg_404 =
"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\"> \
<html><head> \
<title>404 Not Found</title> \
</head><body> \
<h1>404 Not Found</h1> \
</body> \
</html>";

int http_url_handler_404(struct http_connection *hc, int cbtype, void *arg) {
	int		err;

	Dprintf("%s: cbtype %d\n", __FUNCTION__, cbtype);

	/* Answer with 404 page on QUERY */
	if (cbtype == HCB_QUERY) {
		err=http_return_simple(hc,
				"404 Not found",
				"text/html",
				http_static_msg_404,
				strlen(http_static_msg_404));
		return 1;
	}

	/*
	 * Drop connection if socket gets writeable again
	 * or we have an error
	 */
	http_request_end(hc);

	return 1;
}


size_t http_get_queue(struct http_connection *hc) {
	return EVBUFFER_LENGTH(hc->bev->output);
}

/*
 * Function to return an http chunk with previous unknown size. HTTP/1.1 allows
 * for chunked transfers which HTTP/1.0 doesnt know about. There is no legal way
 * to do this so we hope our best.
 *
 */
int http_return_stream(struct http_connection *hc, void *data, size_t datalen) {
	struct evbuffer *evb;

	evb=evbuffer_new();

	/* We are chunked and done sending - end session */
	if (!data) {
		/* Chunked transfer - send a 0 chunk */
		if (hc->proto == HP_HTTP11) {
			evbuffer_add_printf(evb,"0\r\n\r\n");
			bufferevent_write_buffer(hc->bev, evb);
		}
		evbuffer_free(evb);
		hc->status=HC_STATUS_END;
		return 1;
	}

	/* In case of Transfer-Encoding: chunked send a chunk - otherwise just data */
	if (hc->proto == HP_HTTP11) {
		evbuffer_add_printf(evb,"%x\r\n", datalen);
		evbuffer_add(evb, data, datalen);
		evbuffer_add_printf(evb, "\r\n");
	} else {
		evbuffer_add(evb, data, datalen);
	}

	bufferevent_write_buffer(hc->bev, evb);

	Dprintf("%s: output buffer len %d\n", __FUNCTION__,
			EVBUFFER_LENGTH(hc->bev->output));

	evbuffer_free(evb);
	return 1;
}

int http_header_add(struct http_connection *hc, char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	hc->hsize+=vsprintf(&hc->hdr[hc->hsize], fmt, ap);
	hc->hdr[hc->hsize++]=0x0d;
	hc->hdr[hc->hsize++]=0x0a;
	hc->hdr[hc->hsize]=0x00;
	va_end(ap);
	return 0;
}

int http_header_end(struct http_connection *hc) {
	if (hc->keepalive)
		http_header_add(hc, "Connection: keep-alive");
	else
		http_header_add(hc, "Connection: close");

	hc->hsize+=sprintf(&hc->hdr[hc->hsize], "\r\n");
	bufferevent_write(hc->bev, hc->hdr, hc->hsize);
	hc->hsize=0;
	return 0;
}

int http_header_clength(struct http_connection *hc, ssize_t length) {
	if (length >= 0)
		http_header_add(hc, "Content-Length: %d", length);
	else {
		if (hc->proto == HP_HTTP11)
			http_header_add(hc, "Transfer-Encoding: chunked");
	}
	return 0;
}

int http_header_nocache(struct http_connection *hc) {
	http_header_add(hc, "Pragma: no-cache");
	http_header_add(hc, "Cache-Control: no-cache");
	return 0;
}

int http_header_start(struct http_connection *hc, char *result, char *type) {
	struct tm	*tm;
	time_t		t;

	/* Wed, 17 May 2006 17:33:00 GMT */
	t=time(NULL);
	tm=gmtime(&t);

	hc->hsize=0;
	hc->hsize+=sprintf(&hc->hdr[hc->hsize],
			"HTTP/1.1 %s\r\n", result);
	hc->hsize+=sprintf(&hc->hdr[hc->hsize],
			"Date: ");
	hc->hsize+=strftime(&hc->hdr[hc->hsize],
			MAX_HEADER_SIZE-hc->hsize,
			"%a, %d %b %Y %H:%M:%S %Z\r\n", tm);

	http_header_add(hc, "Content-Type: %s", type);

	return 0;
}

/*
 * Send a simple request answer:
 * Content-Length: is set - No chunked, partial transfer,
 * After requests gets answered - connection will be closed
 *
 */
int http_return_simple(struct http_connection *hc, char *result,
		char *type, void *data, size_t datalen) {

	http_header_start(hc, result, type);
	http_header_clength(hc, datalen);
	http_header_end(hc);

	bufferevent_write(hc->bev, data, datalen);

	hc->status=HC_STATUS_END;
	return 1;
}

static int http_process_query(struct http_connection *hc) {
	struct http_url *hu;

	/*
	 * FIXME Need to parse url to parameters, unescaping
	 * characters etc ...
	 *
	 */
	hu=g_hash_table_lookup(hc->server->urls, hc->url);

	Dprintf("%s: hu %p\n", __FUNCTION__, hu);

	if (hu) {
		hc->url_handler=hu->cb;
		return hc->url_handler(hc, HCB_QUERY, hu->arg);
	}

	return hc->url_handler(hc, HCB_QUERY, NULL);
}

static int http_read_head(struct http_connection *hc, struct bufferevent *bev) {
	struct evbuffer	*input = EVBUFFER_INPUT(bev);
	u_char		*eol, *p;
	int		err, drainlen;

	while((eol=evbuffer_find(input, (u_char *) "\n", 1)) != NULL) {

		/* get begin of line */
		p=EVBUFFER_DATA(input);

		/* Line length */
		drainlen=eol-p+1;

		/*
		 * Remove \n and if exists the \r
		 * FIXME Is this robust enough ?
		 */
		*eol=0x0;
		if (eol > p && *(eol-1) == '\r')
			*(--eol) = 0x0;

		if (!hc->url) {
			/* Did we have the first line with command and url ? */
			err=http_parse_command_url(hc, input);
		} else if (eol == p) {
			err=http_process_query(hc);
		} else {
			err=http_parse_attrib(hc, input);
		}

		/* tell buffer to remove leading bytes */
		evbuffer_drain(input, drainlen);

		if (!err)
			return 0;
	}

	return 1;
}

/*
 * Free Request based resources from the http_connection
 *
 */
static void http_request_free(struct http_connection *hc) {
	GList			*item;
	struct http_attrib	*ha;

	if (hc->url)
		free(hc->url);
	hc->url=NULL;

	/* Free http attributes */
	while((item=g_list_first(hc->attrib))) {
		hc->attrib=g_list_remove_link(hc->attrib, item);

		ha=item->data;
		free(ha->token);
		free(ha->value);
		free(ha);

		g_list_free(item);
	}
}

/*
 * Drop the connection in case of error of non keepalive
 */
void http_drop_connection(struct http_connection *hc) {
	struct http_server	*hs=hc->server;

	http_request_free(hc);

	Dprintf("%s:%d\n", __FUNCTION__, __LINE__);
	hs->conn=g_list_remove(hs->conn, hc);

	bufferevent_disable(hc->bev, EV_READ|EV_WRITE);
	bufferevent_free(hc->bev);

	close(hc->fd);
}

/*
 * Application signals http request end - either
 * drop the connection or simply free the request
 * resources.
 *
 */
void http_request_end(struct http_connection *hc) {
	if (!hc->keepalive) {
		http_drop_connection(hc);
	} else {
		http_request_free(hc);
		hc->status=HC_STATUS_HEAD;
		hc->url_handler=&http_url_handler_404;
		hc->hsize=0;
	}
}

/*
 * Callback handler for EV_READ of the client connection. If data
 * arrives for our handler
 */
static void http_cb_read(struct bufferevent *bev, void *arg) {
	struct http_connection	*hc=arg;
	Dprintf("%s:%d\n", __FUNCTION__, __LINE__);
	switch(hc->status) {
		case(HC_STATUS_END):
			hc->url_handler(hc, HCB_END, hc->arg);
			break;
		case(HC_STATUS_HEAD):
			if (!http_read_head(hc, bev))
				hc->url_handler(hc, HCB_ERROR, hc->arg);
			break;
		case(HC_STATUS_BODY):
			hc->url_handler(hc, HCB_READ, hc->arg);
			break;
	}
}

static void http_cb_write(struct bufferevent *bev, void *arg) {
	struct http_connection	*hc=arg;
	Dprintf("%s:%d\n", __FUNCTION__, __LINE__);

	if (hc->status != HC_STATUS_END)
		hc->url_handler(hc, HCB_WRITE, hc->arg);

	if (hc->status == HC_STATUS_END)
		hc->url_handler(hc, HCB_END, hc->arg);
}

static void http_cb_error(struct bufferevent *bev, short what, void *arg) {
	struct http_connection	*hc=arg;
	Dprintf("%s:%d\n", __FUNCTION__, __LINE__);
	hc->url_handler(hc, HCB_ERROR, hc->arg);
}

static void http_connect(int fd, short ev, void *arg) {
	struct http_server	*hs=arg;
	struct http_connection	*hc;
	struct sockaddr_in	sin;
	socklen_t		slen=sizeof(struct sockaddr_in);
	int			nfd;
	unsigned long		flags;

	nfd=accept(fd, (struct sockaddr *) &sin, &slen);

	if (nfd < 0)
		return;

	Dprintf("%s:%d New connection\n", __FUNCTION__, __LINE__);

	/* Set new socket O_NONBLOCK */
	flags=fcntl(nfd, F_GETFL);
	fcntl(nfd, F_SETFL, flags | O_NONBLOCK);

	hc=calloc(1, sizeof(struct http_connection));
	hc->fd=nfd;
	hc->server=hs;
	hc->status=HC_STATUS_HEAD;
	hc->cid=hs->cid++;

	/* Copy remote endpoint sockaddr to http_connection structure */
	memcpy(&hc->sin, &sin, sizeof(struct sockaddr_in));

	hc->bev=bufferevent_new(hc->fd, http_cb_read, http_cb_write, http_cb_error, hc);
	bufferevent_enable(hc->bev, EV_READ);

	hc->url_handler=&http_url_handler_404;

	hs->conn=g_list_append(hs->conn, hc);
}

int http_register_url(struct http_server *hs, struct http_url *hu) {
	while(hu->url) {
		g_hash_table_insert(hs->urls, hu->url, hu);
		hu++;
	}
	return 1;
}

struct http_server *http_init(int port) {
	struct http_server	*hs;
	unsigned long		flags;
	int			one=1;

	hs=calloc(1, sizeof(struct http_server));
	hs->port=port;
	hs->urls=g_hash_table_new(g_str_hash, g_str_equal);

	hs->fd=socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	setsockopt(hs->fd, SOL_SOCKET, SO_REUSEADDR,
			(void *) &one, sizeof(int));

	hs->sin.sin_family=AF_INET;
	hs->sin.sin_port=htons(port);
	hs->sin.sin_addr.s_addr=INADDR_ANY;

	if (bind(hs->fd, (struct sockaddr *) &hs->sin,
				sizeof(struct sockaddr_in)))
		return NULL;

	if (listen(hs->fd, 3))
		return NULL;

	flags=fcntl(hs->fd, F_GETFL);
	fcntl(hs->fd, F_GETFL, flags | O_NONBLOCK);

	event_set(&hs->ev, hs->fd, EV_READ|EV_PERSIST, http_connect, (void *) hs);
	event_add(&hs->ev, NULL);

	return hs;
}
