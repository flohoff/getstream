
/*
 * TODO:
 * 	- token has childs although validate entrys say no childs
 * 	  -> need to error
 *
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "libconf.h"

enum {
	TOKEN_STRING,
	TOKEN_QSTRING,
	TOKEN_COLON,
	TOKEN_OBRACKET,
	TOKEN_CBRACKET,
	TOKEN_NEWLINE,
	TOKEN_EOF,
	TOKEN_QSTRING_EOL,
	TOKEN_QSTRING_EOF,
	TOKEN_QSTRING_BROKEN
};

static void dumpconfig(struct lc_centry *c) {
	printf("token %p\n", c);
	printf("\tprev %p next %p\n", c->prev, c->next);
	printf("\tchild %p parent %p\n", c->child, c->parent);
	printf("\ttoken %s value %s\n", c->token, c->value);
	if(c->child)
		dumpconfig(c->child);
	if(c->next)
		dumpconfig(c->next);
}

static int gettoken(char *c, off_t len, char *tb, off_t *off) {
	off_t	o=*off,
		ts;
	char	*tbp=NULL;

	while(o<len) {
		switch(c[o]) {
			/* Spaces */
			case(' '):
			case('\t'):
				o++;
				break;
			/* Comment - Skip until end of line */
			case('#'):
				while(o<len && c[o] != '\n')
					o++;
				if (o>=len)	/* EOF ? */
					return TOKEN_EOF;
				break;
			/* Quoted string */
			case('"'):
				o++;
				ts=o;
				if (tbp)
					return TOKEN_QSTRING_BROKEN;
				tbp=tb;
				/* Seek closing quote
				 * FIXME What about quoted chars ?
				 */
				while(o<len) {
					switch(c[o]) {
						case('\n'):
							return TOKEN_QSTRING_EOL;
						case('"'):
							*off=++o;
							*tbp++=0x0;
							return TOKEN_QSTRING;
						default:
							*tbp++=c[o++];
							break;
					}
				}
				return TOKEN_QSTRING_EOF;
			/* Newline */
			case('\n'):
				*off=++o;
				return TOKEN_NEWLINE;
			case(';'):
				*off=++o;
				return TOKEN_COLON;
			case('}'):
				*off=++o;
				return TOKEN_CBRACKET;
			case('{'):
				*off=++o;
				return TOKEN_OBRACKET;
			/* non Quoted Token */
			default:
				tbp=tb;
				while(o<len) {
					switch(c[o]) {
						/* Single char tokens */
						case(';'):
						case('}'):
						case('{'):
						case('\n'):
						case(' '):
						case('\t'):
						case('#'):
							*off=o;
							*tbp++=0x0;
							return TOKEN_STRING;
						default:
							*tbp++=c[o++];
							break;
					}
				}

				
		}
	}
	return TOKEN_EOF;
}

struct lc_centry *libconf_parse(char *c, off_t len) {
	off_t			off=0;
	char			tb[256];
	int			te, line=1;
	struct lc_centry	*config=NULL,
				*cce=NULL,
				*lce=NULL;

	cce=calloc(1, sizeof(struct lc_centry));
	config=cce;

	while(1) {
		te=gettoken(c, len, tb, &off);

		switch(te) {
			case(TOKEN_NEWLINE):
				line++;
				break;
			case(TOKEN_OBRACKET):
				lce=cce;
				cce=calloc(1, sizeof(struct lc_centry));
				cce->parent=lce;
				lce->child=cce;
				break;
			case(TOKEN_CBRACKET):
				if (!cce->parent) {
					fprintf(stderr, "To many closing brackets in line %d\n", line);
					return NULL;
				}
				/* Empty {} ? */
				if (!cce->token && !cce->value && cce->parent) {
					/* Remove empty child ce */
					cce=cce->parent;
					free(cce->child);
					cce->child=NULL;

					/* Get next token - hopefully ; */
					break;
				}
				/* Did we close last token/value pair ? */
				if (!cce->closed) {
					fprintf(stderr, "Token not finished e.g. missing ; in line %d\n", line);
					return NULL;
				}

				cce=cce->parent;
				break;
			case(TOKEN_COLON):
				/* Close token */
				cce->closed=1;
				break;
			case(TOKEN_STRING):
				/* Last token was closed - we need a new one */
				if (cce->closed) {
					lce=cce;
					cce=calloc(1, sizeof(struct lc_centry));
					cce->prev=lce;
					lce->next=cce;
					cce->parent=lce->parent;
				}
				if (!cce->token) {
					cce->token=strdup(tb);
					cce->tline=line;
				} else if (!cce->value) {
					cce->value=strdup(tb);
					cce->vline=line;
				} else {
					fprintf(stderr, "Double value for token in line %d\n", line);
					return NULL;
				}
				break;
			case(TOKEN_QSTRING):
				if (!cce || !cce->token) {
					fprintf(stderr, "Quoted token in line %d\n", line);
					return NULL;
				} else if (!cce->value) {
					cce->value=strdup(tb);
					cce->vline=line;
				} else {
					fprintf(stderr, "Double value for token in line %d\n", line);
					return NULL;
				} 
				break;
			case(TOKEN_EOF):
				return config;
			default:
				fprintf(stderr, "failed reading config %d\n", te);
				return NULL;
		}
	}

	/* Unreachable */
	return NULL;
}

static int libconf_validate_value(struct lc_centry *ce, 
		struct lc_ventry *ve) {

	if (!ve->name) {
		fprintf(stderr, "Unknown config option %s in line %d\n", ce->token, ce->tline);
		return 0;
	}

	/* We dont have a value but we would need one */
	if (ve->type != LCV_NONE && (!ce->value && !(ve->opt & LCO_OPTIONAL))) {
		fprintf(stderr, "Config value for option %s in line %d is missing\n", ve->name, ce->tline);
		return 0;
	}

	if (ce->value) {
		switch(ve->type) {
			case(LCV_NONE):
				fprintf(stderr, "Config option %s does not allow value in line %d\n",
						ve->name, ce->vline);
				return 0;
			case(LCV_STRING):
				ce->cbvalue.string=ce->value;
				break;
			case(LCV_BOOL): {
				if ((strcasecmp("true", ce->value) == 0) ||
					(strcasecmp("yes", ce->value) == 0) ||
					(strcasecmp("on", ce->value) == 0) ||
					(strcasecmp("1", ce->value) == 0)) {

					ce->cbvalue.num=1;

				} else if((strcasecmp("false", ce->value) == 0) ||
					(strcasecmp("no", ce->value) == 0 ) ||
					(strcasecmp("off", ce->value) == 0 ) ||
					(strcasecmp("0", ce->value) == 0)) {

					ce->cbvalue.num=0;

				} else {
					fprintf(stderr, "Value %s for boolean option %s is invalid in line %d\n",
							ce->value, ve->name, ce->vline);
					return 0;
				}
				break;
			}
			case(LCV_NUM): {
				char 		*endptr;
				unsigned long	num;
				num=strtol(ce->value, &endptr, 0);
				if (*endptr != 0x0) {
					fprintf(stderr, "Value %s for option %s is not a number in line %d\n", 
							ce->value, ve->name, ce->vline);
					return 0;
				}

				ce->cbvalue.num=num;
				break;
			}
			case(LCV_HEX): {
				char 		*endptr;
				unsigned long	num;
				num=strtol(ce->value, &endptr, 16);
				if (*endptr != 0x0) {
					fprintf(stderr, "Value %s for option %s is not a hex number in line %d\n", 
							ce->value, ve->name, ce->vline);
					return 0;
				}

				ce->cbvalue.num=num;
				break;
			}
			case(LCV_IPADDR):
			case(LCV_IPV6ADDR):
			case(LCV_IPV4ADDR): {
				struct addrinfo *ai;
				int		rc;

				rc=getaddrinfo(ce->value, NULL, NULL, &ai);

				if (rc) {
					fprintf(stderr, "Value %s for option %s is not an ip address in line %d\n", 
							ce->value, ve->name, ce->vline);
					return 0;
				}

				if (ve->type == LCV_IPV6ADDR &&
					ai->ai_family != PF_INET6) {
					fprintf(stderr, "Value %s for option %s is not an ip version 6 address in line %d\n", 
							ce->value, ve->name, ce->vline);
					freeaddrinfo(ai);
					return 0;
				}

				if (ve->type == LCV_IPV4ADDR &&
					ai->ai_family != PF_INET) {
					fprintf(stderr, "Value %s for option %s is not an ip version 4 address in line %d\n", 
							ce->value, ve->name, ce->vline);
					freeaddrinfo(ai);
					return 0;
				}

				ce->cbvalue.string=ce->value;

				freeaddrinfo(ai);

				break;
			}
		}
	}

	return 1;
}

static struct lc_ventry *libconf_find_token(struct lc_centry *ce, 
		struct lc_ventry *ventry) {
	struct	lc_ventry	*ve;

	for(ve=ventry;ve->name;ve++) {
		/* Do we know this config entry ? */
		if (strcasecmp(ce->token, ve->name) == 0)
			break;
	}

	return (ve->name) ? ve : NULL;
}

/*
 * Recurse through parsed config entrys and check tokens 
 * Fill in ventry pointer into centrys
 *
 */
static int libconf_validate_token(struct lc_centry *centry, 
		struct lc_ventry *ventry) {

	struct lc_ventry 	*ve;
	struct lc_centry 	*ce;

	/* Loop on config option entrys */
	for(ce=centry;ce;ce=ce->next) {
		/* Find token */
		ve=libconf_find_token(ce, ventry);
		if (!ve) {
			fprintf(stderr, "Unknown config option %s in line %d\n",
					ce->token, ce->tline);
			return 0;
		}

		/* Fill ventry in centry */
		ce->ventry=ve;
		/* Recurse into children */
		if (ce->child) {
			if(!ve->child) {
				fprintf(stderr, "Config option %s does not allow suboptions in line %d\n", ve->name, ce->tline);
				return 0;
			}
			/* Recurse into childs */
			if (!libconf_validate_token(ce->child, ve->child))
				return 0;
		}

		centry->noce++;
	}

	return 1;
}

/* Recurse through centrys and validate values */
static int libconf_validate_values(struct lc_centry *centry) {
	struct lc_centry	*ce;

	for(ce=centry;ce;ce=ce->next) {
		if (!libconf_validate_value(ce, ce->ventry))
			return 0;
		if (ce->child)
			if (!libconf_validate_values(ce->child))
				return 0;
	}

	return 1;
}

static int libconf_validate_valueopt(struct lc_centry *centry, 
		struct lc_ventry *ventry) {
	struct lc_centry	*ce;
	struct lc_ventry	*ve;
	char			**tuval;

	/* First confentry the *child points to has a noce field */
	tuval=calloc(centry->noce+1, sizeof(char *));

	for(ve=ventry;ve->name;ve++) {
		int	optcount=0, 
			optline=0, 
			valcount=0,
			i;

		for(ce=centry;ce;ce=ce->next) {
			/* Check prefilled ventry ptr */
			if (ce->ventry != ve)
				continue;

			optcount++;
			optline=ce->tline;

			/* Check values */
			if ((ve->opt & LCO_UNIQ) && (ce->value)) {
				/* Check stored value pointers for uniqueness */
				for(i=0;i<valcount;i++) {
					if (strcasecmp(ce->value, tuval[i]) == 0) {
						fprintf(stderr, "Value %s for option %s in line %d is not unique\n",
								ce->value, ve->name, ce->vline);
						return 0;
					}
				}

				/* Store pointer for later uniqueness check */
				tuval[valcount++]=ce->value;
			}
		}

		if (ve->max && optcount > ve->max) {
			fprintf(stderr, "Option %s is only allowed %d times in line %d\n",
					ve->name, ve->max, optline);
			return 0;
		}
		if (ve->min && optcount < ve->min) {
			/* FIXME - line is not really nice */
			fprintf(stderr, "Option %s is needed %d times\n",
					ve->name, ve->min);
			return 0;
		}
	}

	free(tuval);

	for(ce=centry;ce;ce=ce->next) {
		if (ce->child)
			if (!libconf_validate_valueopt(ce->child, 
						ce->ventry->child))
				return 0;
	}

	return 1;
}

/*
 * Run through the parsed and validated tree and 
 * call applications callback functions
 *
 */
static int libconf_validate_callback(struct lc_centry *centry) {
	struct lc_centry *ce;

	for(ce=centry;ce;ce=ce->next) {
		if (ce->ventry->cback)
			if (!ce->ventry->cback(ce, &ce->cbvalue)) 
				return 0;

		if (ce->child)
			if (!libconf_validate_callback(ce->child))
				return 0;

		/* Does application want a late callback ? */	
		if (ce->ventry->opt & LCO_LATECB)
			if (!ce->ventry->cback(ce, NULL)) 
				return 0;
	}

	return 1;
}

int libconf_validate(struct lc_centry *ce, struct lc_ventry *ve) {
	/* Recurse through config entrys - check tokens
	 * and fill ventry pointer */
	if (!libconf_validate_token(ce, ve))
		return 0;

	/* Recurse through config entrys - check values for syntax */
	if (!libconf_validate_values(ce))
		return 0;

	/* Recurse through config and ventry and check for value options 
	 * e.g. uniqueness, min and max count
	 */
	if (!libconf_validate_valueopt(ce, ve))
		return 0;

	/* Recurse through config and call user callbacks */
	if (!libconf_validate_callback(ce))
		return 0;

	return 1;
}

/* Free all libconf related structures */
void libconf_free(struct lc_centry *ce) {
	struct lc_centry *cce=ce, *next;

	while(cce) {
		/* Recurse into children */
		if (cce->child)
			libconf_free(cce->child);

		/* Save next pointer */
		next=cce->next;

		/* Free current centry */
		if (cce->token)
			free(cce->token);
		if (cce->value)
			free(cce->value);
		free(cce);

		cce=next;
	}

	return;
}
