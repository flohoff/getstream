

enum {
	LCV_NONE,
	LCV_NUM,
	LCV_BOOL,
	LCV_HEX,
	LCV_STRING,
	LCV_IPV4ADDR,
	LCV_IPV6ADDR,
	LCV_IPADDR
};

#define	LCO_OPTIONAL	(1<<0)	/* Value is optional */
#define	LCO_UNIQ	(1<<1)	/* Value must be locally unique */
#define	LCO_LATECB	(1<<2)	/* Call childrens callback first */

struct lc_value {
	union {
		long		num;		/* NUM/HEX */
		char		*string;	/* String and Addresses*/
	};
};

struct lc_centry;

struct lc_ventry {
	char				*name;		/* token name */
	int				min, max;	/* min max occurance */
	int				type;		/* type int/string/ipaddr */
	int				opt;		/* options */
	struct lc_ventry		*child;		/* child structures */
	int				(*cback)(struct lc_centry *ce, struct lc_value *val);
};

struct lc_centry {
	struct lc_centry	*prev,*next,
				*child,*parent;
	char			*token,
				*value;
	int			closed,
				tline, 		/* Line# of token */
				vline,		/* Line# of value */
				noce;		/* # of confentrys */

	struct lc_ventry	*ventry;	

	/* Pre parsed values */
	struct lc_value		cbvalue;
};


struct lc_centry *libconf_parse(char *c, off_t len);
int libconf_validate(struct lc_centry *ce, struct lc_ventry *ve);
void libconf_free(struct lc_centry *ce);
