#ifndef SAP_H
#define SAP_H 1

#include "getstream.h"
#include "output.h"

struct sappkt_s {
	uint8_t	version:3,
		addrtype:1,
		reserved:1,
		msgtype:1,
		encryption:1,
		compressed:1;
	uint8_t		authlen;
	uint16_t	msgidhash;
	uint32_t	origin;
};

struct sap_s {
	/* Config items */
	int			scope;
	char			*group;
	int			port;

	char			*announcehost;
	int			announceport;
	char			*playgroup;

	int			interval;

	char			*uri;
	char			*description;
	GList			*emaillist;
	GList			*phonelist;
	GList			*attributelist;

	/* SAP socket port and address */
	int			fd,
				ttl;

	struct event		event;

	int			sid;	/* Session Identifier */
	char			*name;	/* Session Name */
	char			*cdata;	/* Connection Data RFC2327 */
	char			*mdata;	/* Media Announcement Data RFC2327 */
	char			*odata;	/* Origin Data RFC2327 */

	uint32_t		originatingaddr; /* Originating Address for the SAP header */

	struct output_s		*output;
};

#define SAP_ADDRTYPE_V4		0
#define SAP_ADDRTYPE_V6		1

#define SAP_MSGTYPE_ANNOUNCE	1
#define SAP_MSGTYPE_DELETE	0

#define SAP_V4_GLOBAL_ADDRESS   "224.2.127.254"
#define SAP_V4_ORG_ADDRESS      "239.195.255.255" /* Organization-local SAP address */
#define SAP_V4_LOCAL_ADDRESS    "239.255.255.255" /* Local (smallest non-link-local scope) SAP address */
#define SAP_V4_LINK_ADDRESS     "224.0.0.255"	/* Link-local SAP address */

#define SAP_TTL		15
#define SAP_PORT	9875				/* As per RFC 2974 */
#define SAP_MAX_SIZE	1024				/* As per RFC 2974 */

int sap_init(struct sap_s *sap);

enum {
	SAP_SCOPE_NONE = 0,
	SAP_SCOPE_GLOBAL,
	SAP_SCOPE_ORG,
	SAP_SCOPE_LOCAL,
	SAP_SCOPE_LINK
};

#endif
