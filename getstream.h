#ifndef GETSTREAM_H
#define GETSTREAM_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/dvb/version.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include <event.h>
#include <glib.h>

#include "sap.h"
#include "psi.h"

enum {
	AT_DVBS,
	AT_DVBS2,
	AT_DVBT,
	AT_DVBC
};

enum {
	POL_H,
	POL_V
};

#define DVBC_SET_SYMBOLRATE(a, b)	(a)->u.qam.symbol_rate=(b)
#define DVBC_SET_MODULATION(a, b)	(a)->u.qam.modulation=(b)
#define DVBC_SET_FEC(a, b)		(a)->u.qam.fec_inner=(b)

#define DVBT_SET_BANDWIDTH(a, b)	(a)->u.ofdm.bandwidth=(b)
#define DVBT_SET_MODULATION(a, b)	(a)->u.ofdm.constellation=(b)
#define DVBT_SET_TMODE(a, b)		(a)->u.ofdm.transmission_mode=(b)
#define DVBT_SET_GUARD(a, b)		(a)->u.ofdm.guard_interval=(b)
#define DVBT_SET_HIERARCHY(a, b)	(a)->u.ofdm.hierarchy_information=(b)
#define DVBT_SET_CODERATE_HP(a, b)	(a)->u.ofdm.code_rate_HP=(b)
#define DVBT_SET_CODERATE_LP(a, b)	(a)->u.ofdm.code_rate_LP=(b)

#define DVBS_SET_SYMBOLRATE(a, b)	(a)->u.qpsk.symbol_rate=(b)
#define DVBS_SET_FEC(a, b)		(a)->u.qpsk.fec_inner=(b)

#define DEBUG

#ifdef DEBUG
#define dprintf		printf
#else
#define dprintf( a... )
#endif

#define	MAX_CTL_MSG_SIZE	1500

/*
 *
 *
 * getstream.c
 *
 *
 */

extern int loglevel;

#define MAX_MCAST_PAYLOAD	(1500-40)
#define TS_PACKET_SIZE		188
#define DVR_BUFFER_DEFAULT	50		/* I have seen an average of 44 Packets per read */
#define PID_MAX			0x1fff
#define	DVR_DEFAULT_STUCKINT	5000		/* 5 seconds stuck check interval */

enum {
	PID_TYPE_NONE,
	PID_TYPE_PAT,
	PID_TYPE_SDT,
	PID_TYPE_PMT,
	PID_TYPE_PCR,
	PID_TYPE_VIDEO,
	PID_TYPE_AUDIO,
	PID_TYPE_PRIVATE,
	PID_TYPE_USER,
	PID_TYPE_STATIC,
	PID_TYPE_REASSEMBLE,
	PID_TYPE_OTHER,
};

enum {
	DVRCB_TS,
	DVRCB_SECTION,
};

extern const char	*pidtnames[];

struct pat_s {
	uint16_t		tid;
	unsigned int		progcount;
	GList			*program;
};

enum {
	INPUT_NONE,
	INPUT_PID,
	INPUT_PNR,
	INPUT_FULL
};

struct input_s {
	struct stream_s	*stream;

	int		type;

	struct {
		uint16_t	pid;
		void		*cbkey;
	} pid;
	struct {
		uint16_t	pnr;
		void		*program;
	} pnr;
};

struct stream_s {
	char			*name;
	struct adapter_s	*adapter;
	GList			*output;

	GList			*input;
	int			psineeded;	/* True if PNR inputs - we need to generate PAT & PMT(s) */

	uint8_t			patcc;
	struct event		patevent;
};

struct dvrpt_s {
	GList		*callback;
	GList		*sectioncallback;
	unsigned long	packets;

	struct psisec_s	*section;
	unsigned int	secuser;
	void		*seccb;
};

struct adapter_s {
	int			no;		/* Adapter Number */
	int			type;		/* Adapter Type - DVB-S/DVB-T/DVB-C */

	int			budgetmode;
	GList			*streams;

	/* fe.c */
	struct {
		int				fd;
		struct event			timer;
		struct event			event;
		time_t				tunelast;

		struct dvb_frontend_info	feinfo;

		union {
			struct {					/* Tuning information DVB-S */
				int		lnbsharing;
				unsigned long	lnb_lof1;
				unsigned long	lnb_lof2;
				unsigned long	lnb_slof;

				unsigned long	t_freq;
				int		t_pol;			/* POL_H || POL_V */
				unsigned long	t_srate;
				int		t_diseqc;
			} dvbs;

			struct {					/* Tuning information DVB-T */
				unsigned long	freq;
				int		bandwidth;		/* 0 (Auto) 6 (6Mhz), 7 (7Mhz), 8 (8Mhz) */
				int		tmode;			/* 0 (Auto) 2 (2Khz), 8 (8Khz) */
				int		modulation;		/* 0, 16, 32, 64, 128, 256 QUAM */
				int		guard;			/* 0, 4, 8, 16, 32 1/x Guard */
				int		hierarchy;		/* 0, -1, 1, 2, 4 - 0 Auto, -1 None */
			} dvbt;

			struct {					/* Tuning information DVB-C */
				unsigned long	freq;
				unsigned long	srate;
				int		modulation;		/* -1 (QPSK), 0, 16, 32, 64, 128, 256 QAM  */
				int		fec;			/* 0 (Auto) - 9 */
			} dvbc;
		};
	} fe;

	/* dvr */
	struct {
		int			fd;
		struct event		dvrevent;

		struct event		stucktimer;
		time_t			lastinput;
		int			stuckinterval;

		int			dvrbuffer;

		/* List of full stream callbacks */
		GList			*fullcb;

		struct dvrpt_s		pidtable[PID_MAX+1];

		struct {
			int		size;		/* Config option */
			uint8_t		*ptr;
		} buffer;

		uint8_t			pidcc[PID_MAX+1];

		struct {
			unsigned int		reads;		/* Reads in the stats interval */
			int			interval;	/* Config option */
			time_t			last;
			struct event		event;
		} stat;
	} dvr;

	struct {
		struct event		dmxevent;
		int			buffersize;
		/* dmx */
		struct {
			int	fd;
			int	type;
		} pidtable[PID_MAX+1+1];			/* Space for 0x2000 pid structures */
	} dmx;

	/* PAT */
	struct {
		void		*cbc;
		struct psi_s	psi;
		struct pat_s	*last;
		struct pat_s	*current;
	} pat;

	struct {
		GList		*pnrlist;
	} pmt;

	struct {
		void		*cbc;
		struct psi_s	psi;
	} sdt;
};

/*
 *
 *
 * demux.c
 *
 *
 *
 */
void demux_init(struct adapter_s *adapter);
void *demux_add_pid(struct adapter_s *a,
	uint16_t pid, void (*demuxcb)(uint8_t *ts, void *arg), void *arg);
void *demux_add_pnr(struct adapter_s *a,
	uint16_t pnr, void (*demuxcb)(uint8_t *ts, void *arg), void *arg);

/*
 *
 *
 * fe.c
 *
 *
 */
int fe_tune_init(struct adapter_s *adapter);
void fe_retune(struct adapter_s *adapter);

/*
 *
 *
 * util.c
 *
 *
 */

int addr_is_mcast(char *addr);
void dump_hex(int level, const char *prefix, uint8_t *buf, int size);
void ts_packet_decode(uint8_t *ts);

/*
 *
 *
 * stream.c
 *
 *
 *
 */
void stream_init(struct stream_s *stream);
void stream_send(void *data, void *arg);

/*
 *
 *
 * input.c
 *
 *
 */
void input_init(struct input_s *input);

/*
 *
 * logging
 *
 *
 */
void logwrite_inc_level(void );
void logwrite(int level, const char *format, ...);

enum {
	LOG_ERROR,
	LOG_INFO,
	LOG_DEBUG,
	LOG_EVENT,
	LOG_XTREME,
};

/*
 *
 *
 * dvr.c
 *
 *
 *
 */
int dvr_init(struct adapter_s *adapter);
void *dvr_add_pcb(struct adapter_s *a, unsigned int pid, unsigned int type,
		unsigned int pidt, void (*callback)(void *data, void *arg), void *arg);
void dvr_del_pcb(struct adapter_s *a, unsigned int pid, void *cbs);

/*
 *
 *
 * dmx.c
 *
 *
 *
 */
int dmx_init(struct adapter_s *adapter);
int dmx_join_pid(struct adapter_s *a, unsigned int pid, int type);
void dmx_leave_pid(struct adapter_s *a, int pid);
void dmx_bounce_filter(struct adapter_s *adapter);

/*
 *
 * pat.c
 *
 */
void pat_section_add(struct adapter_s *a, struct psisec_s *section);
void pat_addpmtpid(struct pat_s *pat, uint16_t pnr, uint16_t pid);
void pat_free(struct pat_s *pat);
struct pat_s *pat_new();
void pat_add_program(struct pat_s *pat, uint16_t pnr, uint16_t pid);
unsigned int pat_send(struct pat_s *pat, uint8_t cc, uint8_t version, uint16_t tid, void (*callback)(void *data, void *arg), void *arg);
void pat_init(struct adapter_s *adapter);

/*
 *
 * sdt.c
 *
 */
void sdt_init(struct adapter_s *adapter);


/*
 *
 * pmt.c
 *
 */
void pmt_pidfrompat(struct adapter_s *a, unsigned int pnr, unsigned int pmtpid);
void *pmt_join_pnr(struct adapter_s *a, unsigned int pnr, \
			void (*callback)(void *data, void *arg), void *arg);
unsigned int pmt_get_pmtpid(void *program);

#define PID_MASK	0x1fff

#define TS_SYNC_OFF	0
#define TS_SYNC		0x47

#define TS_PID_OFF1	1
#define TS_PID_OFF2	2

#define TS_PUSI_OFF	1
#define TS_PUSI_MASK	0x40

#define TS_AFC_OFF	3
#define TS_AFC_MASK	0x30
#define TS_AFC_SHIFT	4

#define TS_HEAD_MIN	4
#define TS_AFC_LEN	4

#define TS_CC_OFF	3
#define TS_CC_MASK	0xf

#define TS_AF_OFF	4
#define TS_PAYLOAD_OFF	4

static inline unsigned int ts_has_payload(uint8_t *tsp) {
	int	afc;
	afc=(tsp[TS_AFC_OFF] & TS_AFC_MASK) >> TS_AFC_SHIFT;
	return(afc & 0x1);
}

static inline unsigned int ts_afc(uint8_t *tsp) {
	return ((tsp[TS_AFC_OFF] & TS_AFC_MASK) >> TS_AFC_SHIFT);
}

static inline unsigned int ts_has_af(uint8_t *tsp) {
	return (ts_afc(tsp) & 0x2);
}

static inline unsigned int ts_af_len(uint8_t *tsp) {
	return (ts_has_af(tsp) ? tsp[TS_AFC_LEN]+1 : 0);
}

static inline unsigned int ts_payload_start(uint8_t *tsp) {
	return ts_af_len(tsp)+TS_HEAD_MIN;
}

static inline unsigned int ts_pusi(uint8_t *tsp) {
	return tsp[TS_PUSI_OFF] & TS_PUSI_MASK;
}

#define ts_pid(ts) \
	(((ts)[TS_PID_OFF1]<<8|(ts)[TS_PID_OFF2])&PID_MASK)
#if 0
static inline unsigned int ts_pid(uint8_t *tsp) {
	return (tsp[TS_PID_OFF1]<<8|tsp[TS_PID_OFF2])&PID_MASK;
}
#endif
static inline unsigned int ts_tei(uint8_t *tsp) {
	return (tsp[TS_PID_OFF1] & 0x80);
}

static inline unsigned int ts_cc(uint8_t *tsp) {
	return (tsp[TS_CC_OFF] & TS_CC_MASK);
}

#endif
