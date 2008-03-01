#ifndef LIBHTTP_H
#define LIBHTTP_H

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <event.h>

#include <glib/glist.h>
#include <glib/ghash.h>


#define MAX_HEADER_SIZE	3000

enum {
	HC_STATUS_HEAD,
	HC_STATUS_BODY,
	HC_STATUS_END
};

enum {
	HC_CMD_UNKNOWN,
	HC_CMD_GET,
	HC_CMD_POST,
	HC_CMD_HEAD,
};

enum {
	HP_UNKNOWN,
	HP_HTTP,
	HP_HTTP10,
	HP_HTTP11
};

/* Callback types - Why are we calling back */
enum {
	HCB_QUERY,		/* Check METHOD for validity */
	HCB_WRITE,		/* Socket is writeable - send more */
	HCB_READ,		/* Data arrived - parse it */
	HCB_ERROR,		/* We got an error from underneath */
	HCB_END,		/* User said - we want to end - give em a chance to clean up */
};

struct http_attrib {
	char	*token;
	char	*value;
};

struct http_connection {
	struct http_server	*server;
	struct sockaddr_in	sin;
	int			fd,
				status,
				cid,
				request;
	struct bufferevent	*bev;
	struct evbuffer		*evb;
	int			keepalive;

	/* Request information */
	int			cmd;
	char			*url;
	int			proto;
	GList			*attrib;

	/* url handler - set after request received */
	int			(*url_handler)(struct http_connection *hc, int cbtype, void *arg);

	/* Application */
	void			*arg;

	/* Header */
	int			hsize;
	char			hdr[MAX_HEADER_SIZE];
};

struct http_server {
	int			port;
	struct sockaddr_in	sin;
	int			fd;
	struct event		ev;
	int			cid;

	GList			*conn;
	GHashTable		*urls;
};

struct http_url {
	char	*url;
	int	(*cb)(struct http_connection *hc, int cbtype, void *arg);
	void	*arg;
};

struct http_server *http_init(int port);
int http_register_url(struct http_server *hs, struct http_url *hu);
void http_drop_connection(struct http_connection *hc);
int http_return_simple(struct http_connection *hc, char *result, char *type, void *data, size_t datalen);
int http_return_stream(struct http_connection *hc, void *data, size_t datalen);
size_t http_get_queue(struct http_connection *hc);
int http_header_add(struct http_connection *hc, char *fmt, ...);
int http_header_end(struct http_connection *hc);
int http_header_clength(struct http_connection *hc, ssize_t length);
int http_header_nocache(struct http_connection *hc);
int http_header_start(struct http_connection *hc, char *result, char *type);
void http_request_end(struct http_connection *hc);

#endif
