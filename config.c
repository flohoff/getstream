
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include <glib/glist.h>

#include "config.h"
#include "libconf.h"
#include "sap.h"
#include "output.h"
#include "getstream.h"

static struct config_s	*config;

/* Temporary variables for parsing */
static struct adapter_s	*adapter;
static struct output_s	*output;
static struct input_s	*input;
static struct stream_s	*stream;
static struct sap_s	*sap;

static int cf_adapter_start(struct lc_centry *ce, struct lc_value *val) {
	adapter=calloc(1, sizeof(struct adapter_s));
	adapter->no=val->num;
	adapter->budgetmode=1;
	adapter->dvr.stuckinterval=DVR_DEFAULT_STUCKINT;
	adapter->dvr.buffer.size=DVR_BUFFER_DEFAULT*TS_PACKET_SIZE;
	config->adapter=g_list_append(config->adapter, adapter);
	return 1;
}

static int cf_sap_scope(struct lc_centry *ce, struct lc_value *val)
	{ sap->scope=val->string; return 1; }
static int cf_sap_sap_group(struct lc_centry *ce, struct lc_value *val)
	{ sap->group=val->string; return 1; }
static int cf_sap_sap_port(struct lc_centry *ce, struct lc_value *val)
	{ sap->port=val->num; return 1; }
static int cf_sap_ttl(struct lc_centry *ce, struct lc_value *val)
	{ sap->ttl=val->num; return 1; }
static int cf_sap_playgroup(struct lc_centry *ce, struct lc_value *val)
	{ sap->playgroup=val->string; return 1; }
static int cf_sap_announce_host(struct lc_centry *ce, struct lc_value *val)
	{ sap->announcehost=val->string; return 1; }
static int cf_sap_announce_port(struct lc_centry *ce, struct lc_value *val)
	{ sap->announceport=val->num; return 1; }

static int cf_output_remoteport(struct lc_centry *ce, struct lc_value *val)
	{ output->remoteport=val->num; return 1; }
static int cf_output_remoteaddr(struct lc_centry *ce, struct lc_value *val)
	{ output->remoteaddr=val->string; return 1; }
static int cf_output_localaddr(struct lc_centry *ce, struct lc_value *val)
	{ output->localaddr=val->string; return 1; }
static int cf_output_ttl(struct lc_centry *ce, struct lc_value *val)
	{ output->ttl=val->num; return 1; }
static int cf_output_url(struct lc_centry *ce, struct lc_value *val)
	{ output->url=val->string; return 1; }
static int cf_http_port(struct lc_centry *ce, struct lc_value *val)
	{ config->http_port=val->num; return 1; }
static int cf_pipe_filename(struct lc_centry *ce, struct lc_value *val)
	{ output->pipe.filename=val->string; return 1; }

static int cf_sap_start(struct lc_centry *ce, struct lc_value *val) {
	sap=calloc(1, sizeof(struct sap_s));
	output->sap=sap;
	sap->output=output;

	/* Default values */
	sap->ttl=15;
	return 1;
}

static int cf_stream_start(struct lc_centry *ce, struct lc_value *val) {
	stream=calloc(1, sizeof(struct stream_s));
	stream->adapter=adapter;
	adapter->streams=g_list_append(adapter->streams, stream);
	return 1;
}

static int cf_input_start(int itype) {
	input=calloc(1, sizeof(struct input_s));
	input->type=itype;
	stream->input=g_list_append(stream->input, input);
	input->stream=stream;
	return 1;
}

static int cf_input_pid(struct lc_centry *ce, struct lc_value *val) {
	cf_input_start(INPUT_PID);
	input->pid.pid=val->num;
	return 1;
}

static int cf_input_pnr(struct lc_centry *ce, struct lc_value *val) {
	cf_input_start(INPUT_PNR);
	input->pnr.pnr=val->num;
	input->stream->psineeded=1;
	return 1;
}

static int cf_input_full(struct lc_centry *ce, struct lc_value *val) {
	cf_input_start(INPUT_FULL);
	return 1;
}


static int cf_output_start(struct lc_centry *ce, struct lc_value *val, int stype) {
	output=calloc(1, sizeof(struct output_s));
	output->type=stype;
	output->ttl=15;
	output->stream=stream;
	stream->output=g_list_append(stream->output, output);
	return 1;
}

static int cf_output_udp_start(struct lc_centry *ce, struct lc_value *val)
	{ return cf_output_start(ce, val, OTYPE_UDP); }
static int cf_output_rtp_start(struct lc_centry *ce, struct lc_value *val)
	{ return cf_output_start(ce, val, OTYPE_RTP); }
static int cf_output_http_start(struct lc_centry *ce, struct lc_value *val)
	{ return cf_output_start(ce, val, OTYPE_HTTP); }
static int cf_output_pipe_start(struct lc_centry *ce, struct lc_value *val)
	{ return cf_output_start(ce, val, OTYPE_PIPE); }

struct lc_ventry conf_sap[] = {
	{ "scope", 0, 1, LCV_STRING, 0, NULL, cf_sap_scope },
	{ "sap-group", 0, 1, LCV_IPV4ADDR, 0, NULL, cf_sap_sap_group },
	{ "sap-port", 0, 1, LCV_NUM, 0, NULL, cf_sap_sap_port },
	{ "announce-host", 0, 1, LCV_STRING, 0, NULL, cf_sap_announce_host },
	{ "announce-port", 0, 1, LCV_NUM, 0, NULL, cf_sap_announce_port },
	{ "ttl", 0, 1, LCV_NUM, 0, NULL, cf_sap_ttl },
	{ "playgroup", 0, 1, LCV_STRING, 0, NULL, cf_sap_playgroup },
	{ NULL, 0, 0, 0, 0, NULL },
};

struct lc_ventry conf_output_udp[] = {
	{ "local-address", 0, 1, LCV_IPV4ADDR, 0, NULL, cf_output_localaddr },
	{ "remote-address", 1, 1, LCV_IPADDR, 0, NULL, cf_output_remoteaddr },
	{ "remote-port", 1, 1, LCV_NUM, 0, NULL, cf_output_remoteport },
	{ "ttl", 0, 1, LCV_NUM, 0, NULL, cf_output_ttl },
	{ "sap", 0, 1, LCV_NONE, 0, conf_sap, cf_sap_start },
	{ NULL, 0, 0, 0, 0, NULL, NULL },
};

struct lc_ventry conf_output_rtp[] = {
	{ "local-address", 0, 1, LCV_IPV4ADDR, 0, NULL, cf_output_localaddr },
	{ "remote-address", 1, 1, LCV_IPV4ADDR, 0, NULL, cf_output_remoteaddr },
	{ "remote-port", 1, 1, LCV_NUM, 0, NULL, cf_output_remoteport },
	{ "ttl", 0, 1, LCV_NUM, 0, NULL, cf_output_ttl },
	{ "sap", 0, 1, LCV_NONE, 0, conf_sap, cf_sap_start },
	{ NULL, 0, 0, 0, 0, NULL },
};

struct lc_ventry conf_output_http[] = {
	{ "url", 1, 1, LCV_STRING, 0, NULL, cf_output_url },
	{ NULL, 0, 0, 0, 0, NULL },
};

struct lc_ventry conf_output_pipe[] = {
	{ "filename", 1, 1, LCV_STRING, 0, NULL, cf_pipe_filename },
	{ NULL, 0, 0, 0, 0, NULL },
};

static int cf_stream_name(struct lc_centry *ce, struct lc_value *val)
	{ stream->name=val->string; return 1; }

#if 0
static int cf_channel_csa_key(struct lc_centry *ce, struct lc_value *val) {
	char		*eptr;
	uint64_t	key;
	int		i, l=strlen(val->string);

	key=strtoull(val->string, &eptr, 16);

	/*
	 * Was the string parsed ?
	 * Was the string a number until the end ?
	 * Was the string between 16 and 18 (0x) bytes long ?
	 *
	 */
	if (val->string == eptr || (eptr != NULL && eptr[0] != 0x0) || l<16 || l>18) {
		fprintf(stderr, "config: Invalid csa-key \"%s\" in line %d\n",
				val->string, ce->vline);
		return 0;
	}

	/* cpu_to_64be anyone ? */
	for(i=0;i<8;i++)
		channel->csakey[i]=(key>>(56-8*i))&0xff;

	channel->csat=csa_New();
	csa_SetCW(channel->csat, channel->csakey, channel->csakey);

	return 1;
};

static int cf_channel_csa_length(struct lc_centry *ce, struct lc_value *val) {
	if (val->num > TS_PACKET_SIZE) {
		fprintf(stderr, "config: Invalid csa length %ld in line %d. Needs to be between 0 and 188\n", 
			val->num, ce->vline);
		return 0;
	}
	channel->csalength=val->num;
	return 1;
}

static int cf_channel_csa(struct lc_centry *ce, struct lc_value *val)
	{ channel->csalength=TS_PACKET_SIZE; return 1; }

static struct lc_ventry conf_channel_csa[] = {
	{ "key", 0, 1, LCV_STRING, 0, NULL, cf_channel_csa_key },
	{ "length", 0, 1, LCV_STRING, 0, NULL, cf_channel_csa_length },
	{ NULL, 0, 0, 0, 0, NULL },
};
#endif

static struct lc_ventry conf_input[] = {
	{ "pid", 0, 0, LCV_NUM, LCO_UNIQ, NULL, cf_input_pid },
	{ "pnr", 0, 1, LCV_NUM, LCO_UNIQ, NULL, cf_input_pnr },
	{ "full", 0, 1, LCV_NONE, LCO_UNIQ, NULL, cf_input_full },
	{ NULL, 0, 0, 0, 0, NULL },
};

static struct lc_ventry conf_stream[] = {
	{ "name", 1, 1, LCV_STRING, 0, NULL, cf_stream_name },
	{ "sap", 0, 1, LCV_NONE, 0, conf_sap, cf_sap_start },
	{ "input", 0, 1, LCV_NONE, 0, conf_input, NULL },
	{ "output-udp", 0, 0, LCV_NONE, 0, conf_output_udp, cf_output_udp_start },
	{ "output-rtp", 0, 0, LCV_NONE, 0, conf_output_rtp, cf_output_rtp_start },
	{ "output-http", 0, 0, LCV_NONE, 0, conf_output_http, cf_output_http_start },
	{ "output-pipe", 0, 0, LCV_NONE, 0, conf_output_pipe, cf_output_pipe_start },
	{ NULL, 0, 0, 0, 0, NULL },
};

static int cf_dvbs_trans_pol(struct lc_centry *ce, struct lc_value *val) {
	if (strcasecmp(val->string, "h") == 0) {
		adapter->fe.dvbs.t_pol=POL_H;
	} else if(strcasecmp(val->string, "v") == 0) {
		adapter->fe.dvbs.t_pol=POL_V;
	} else {
		logwrite(LOG_ERROR, "Illegal polarization \"%s\" in line %d\n", val->string, ce->vline);
		return 0;
	}

	return 1;
}

static int cf_dvbs_trans_freq(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbs.t_freq=val->num; return 1; }
static int cf_dvbs_trans_srate(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbs.t_srate=val->num; return 1; }
static int cf_dvbs_trans_diseqc(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbs.t_diseqc=val->num; return 1; }
static int cf_dvbs_lnbsharing(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbs.lnbsharing=val->num; return 1; }

static struct lc_ventry conf_dvbs_transponder[] = {
	{ "frequency", 1, 1, LCV_NUM, 0, NULL, cf_dvbs_trans_freq },
	{ "polarisation", 1, 1, LCV_STRING, 0, NULL, cf_dvbs_trans_pol },
	{ "symbol-rate", 1, 1, LCV_NUM, 0, NULL, cf_dvbs_trans_srate },
	{ "diseqc", 0, 1, LCV_NUM, 0, NULL, cf_dvbs_trans_diseqc },
	{ NULL, 0, 0, 0, 0, NULL },
};

static int cf_dvbs_lnb_lof1(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbs.lnb_lof1=val->num; return 1; }
static int cf_dvbs_lnb_lof2(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbs.lnb_lof2=val->num; return 1; }
static int cf_dvbs_lnb_slof(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbs.lnb_slof=val->num; return 1; }

static struct lc_ventry conf_dvbs_lnb[] = {
	{ "lof1", 1, 1, LCV_NUM, 0, NULL, cf_dvbs_lnb_lof1 },
	{ "lof2", 1, 1, LCV_NUM, 0, NULL, cf_dvbs_lnb_lof2 },
	{ "slof", 1, 1, LCV_NUM, 0, NULL, cf_dvbs_lnb_slof },
	{ NULL, 0, 0, 0, 0, NULL },
};

static struct lc_ventry conf_dvbs[] = {
	{ "lnb-sharing", 0, 1, LCV_BOOL, 0, NULL, cf_dvbs_lnbsharing },
	{ "lnb", 1, 1, LCV_NONE, 0, conf_dvbs_lnb, NULL },
	{ "transponder", 1, 1, LCV_NONE, 0, conf_dvbs_transponder, NULL },
	{ NULL, 0, 0, 0, 0, NULL },
};

static int cf_dvbt_bandwidth(struct lc_centry *ce, struct lc_value *val) {
	if (strcasecmp("auto", val->string) == 0) {
		adapter->fe.dvbt.bandwidth=0;
	} else {
		int	bw=strtol(val->string, NULL, 10);
		if (bw != 6 && bw != 7 && bw != 8) {
			fprintf(stderr, "config: Illegal DVB-T bandwidth \"%s\" in line %d\n",
					val->string, ce->vline);
			return 0;
		}
		adapter->fe.dvbt.bandwidth=bw;
	}
	return 1;
}

static int cf_dvbt_freq(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbt.freq=val->num; return 1; }

static int cf_dvbt_tmode(struct lc_centry *ce, struct lc_value *val) {
	if (strcasecmp("auto", val->string) == 0) {
		adapter->fe.dvbt.tmode=0;
	} else {
		int	t=strtol(val->string, NULL, 10);
		if (t != 2 && t != 8) {
			fprintf(stderr, "config: Illegal DVB-T transmission-mode \"%s\" in line %d\n",
					val->string, ce->vline);
			return 0;
		}
		adapter->fe.dvbt.tmode=t;
	}
	return 1;
}

static int cf_dvbt_modulation(struct lc_centry *ce, struct lc_value *val) {
	if (strcasecmp("auto", val->string) == 0) {
		adapter->fe.dvbt.modulation=0;
	} else {
		int	m=strtol(val->string, NULL, 10);
		if (m != 16 && m != 32 && m != 64 && m != 128 && m != 256) {
			fprintf(stderr, "config: Illegal DVB-T modulation \"%s\" in line %d\n",
					val->string, ce->vline);
			return 0;
		}
		adapter->fe.dvbt.modulation=m;
	}
	return 1;
}

static int cf_dvbt_guard(struct lc_centry *ce, struct lc_value *val) {
	if (strcasecmp("auto", val->string) == 0) {
		adapter->fe.dvbt.guard=0;
	} else {
		int	gi=strtol(val->string, NULL, 10);
		if (gi != 4 && gi != 8 && gi != 16 && gi != 32) {
			fprintf(stderr, "config: Illegal DVB-T guard-interval \"%s\" in line %d\n",
					val->string, ce->vline);
			return 0;
		}
		adapter->fe.dvbt.guard=gi;
	}
	return 1;
}

static int cf_dvbt_hierarchy(struct lc_centry *ce, struct lc_value *val) {
	if (strcasecmp("none", val->string) == 0)
		adapter->fe.dvbt.hierarchy=-1;
	else if (strcasecmp("auto", val->string) == 0)
		adapter->fe.dvbt.hierarchy=0;
	else {
		int	h=strtol(val->string, NULL, 0);

		if (h != 1 && h != 2 && h != 4) {
			fprintf(stderr, "config: Illegal DVB-T hierarchy %s in line %d\n",
					val->string, ce->vline);
			return 0;
		}

		adapter->fe.dvbt.hierarchy=h;
	}
	return 1;
}

static struct lc_ventry conf_dvbt[] = {
	{ "bandwidth", 0, 1, LCV_STRING, 0, NULL, cf_dvbt_bandwidth },
	{ "frequency", 1, 1, LCV_NUM, 0, NULL, cf_dvbt_freq },
	{ "transmission-mode", 0, 1, LCV_STRING, 0, NULL, cf_dvbt_tmode },
	{ "modulation", 0, 1, LCV_STRING, 0, NULL, cf_dvbt_modulation },
	{ "guard-interval", 0, 1, LCV_STRING, 0, NULL, cf_dvbt_guard },
	{ "hierarchy", 0, 1, LCV_STRING, 0, NULL, cf_dvbt_hierarchy },
	{ NULL, 0, 0, 0, 0, NULL },
};

static int cf_dvbs(struct lc_centry *ce, struct lc_value *val)
	{ adapter->type=AT_DVBS; return 1; }
static int cf_dvbs2(struct lc_centry *ce, struct lc_value *val)
	{ adapter->type=AT_DVBS2; return 1; }
static int cf_dvbt(struct lc_centry *ce, struct lc_value *val)
	{ adapter->type=AT_DVBT; return 1; }
static int cf_dvbc(struct lc_centry *ce, struct lc_value *val)
	{ adapter->type=AT_DVBC; return 1; }

static int cf_dvbc_freq(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbc.freq=val->num; return 1; }
static int cf_dvbc_modulation(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbc.modulation=val->num; return 1; }
static int cf_dvbc_trans_srate(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbc.srate=val->num; return 1; }
static int cf_dvbc_fec(struct lc_centry *ce, struct lc_value *val)
	{ adapter->fe.dvbc.fec=val->num; return 1; }

static struct lc_ventry conf_dvbc[] = {
	{ "frequency", 1, 1, LCV_NUM, 0, NULL, cf_dvbc_freq },
	{ "modulation", 0, 1, LCV_NUM, 0, NULL, cf_dvbc_modulation },
	{ "symbol-rate", 1, 1, LCV_NUM, 0, NULL, cf_dvbc_trans_srate },
	{ "fec", 0, 1, LCV_NUM, 0, NULL, cf_dvbc_fec },
	{ NULL, 0, 0, 0, 0, NULL },
};

static int cf_adapter_budget(struct lc_centry *ce, struct lc_value *val)
	{ adapter->budgetmode=val->num; return 1; }
static int cf_adapter_packetbuffer(struct lc_centry *ce, struct lc_value *val)
	{ adapter->dvr.buffer.size=val->num; return 1; }
static int cf_adapter_statinterval(struct lc_centry *ce, struct lc_value *val)
	{ adapter->dvr.stat.interval=val->num; return 1; }
static int cf_adapter_stuckinterval(struct lc_centry *ce, struct lc_value *val)
	{ adapter->dvr.stuckinterval=val->num; return 1; }

static struct lc_ventry conf_adapter[] = {
	{ "budget-mode", 0, 1, LCV_BOOL, 0, NULL, cf_adapter_budget },
	{ "packet-buffer", 0, 1, LCV_NUM, 0, NULL, cf_adapter_packetbuffer },
	{ "stat-interval", 0, 1, LCV_NUM, 0, NULL, cf_adapter_statinterval },
	{ "stuck-interval", 0, 1, LCV_NUM, 0, NULL, cf_adapter_stuckinterval },
	{ "stream", 0, 0, LCV_NONE, 0, conf_stream, cf_stream_start },
	{ "dvb-s", 0, 1, LCV_NONE, 0, conf_dvbs, cf_dvbs },
	{ "dvb-s2", 0, 1, LCV_NONE, 0, conf_dvbs, cf_dvbs2 },
	{ "dvb-t", 0, 1, LCV_NONE, 0, conf_dvbt, cf_dvbt },
	{ "dvb-c", 0, 1, LCV_NONE, 0, conf_dvbc, cf_dvbc },
	{ NULL, 0, 0, 0, 0, NULL },
};

static struct lc_ventry conf_http[] = {
	{ "port", 1, 1, LCV_NUM, 0, NULL, cf_http_port },
	{ NULL, 0, 0, 0, 0, NULL },
};

static struct lc_ventry conf_main[] = {
	{ "adapter", 1, 0, LCV_NUM, LCO_UNIQ, conf_adapter, cf_adapter_start },
	{ "http", 1, 1, LCV_NONE, 0, conf_http, NULL },
	{ NULL, 0, 0, 0, 0, NULL },
};

struct config_s *readconfig(char *filename) {
	int			cfd;
	struct stat		sb;
	char			*ctext;
	struct lc_centry	*c;

	/* Base for config storage */
	config=calloc(1, sizeof(struct config_s));

	cfd=open(filename, O_RDONLY);
	if (cfd<0)
		return NULL;

	if (fstat(cfd, &sb)) {
		close(cfd);
		return NULL;
	}

	ctext=mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, cfd, 0);

	c=libconf_parse(ctext, sb.st_size);

	munmap(ctext, sb.st_size);
	close(cfd);

	if (!c)
		return NULL;

	if (!libconf_validate(c, conf_main)) {
		libconf_free(c);
		return NULL;
	}

	return config;
}
