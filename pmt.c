#include <sys/types.h>
#include <malloc.h>

#include <glib.h>

#include "getstream.h"
#include "psi.h"
#include "crc32.h"

struct programcb_s {
	void		(*callback)(void *data, void *arg);
	void		*arg;
};

struct pmtes_s {
	uint8_t		streamtype;
	uint16_t	espid;
};

struct pmt_s {
	uint16_t	pnr;
	uint16_t	pcrpid;
	GList		*es;
};

struct program_s {
	struct adapter_s	*adapter;
	unsigned int		pnr;

	uint16_t		pmtpid;
	void			*pmtpidcb;	/* pid callback cookie (PMT) */

	struct {
		GList	*cb;
	} pidtable[PID_MAX];

	struct psi_s		psi;

	struct pmt_s		*pmtlast;
	struct pmt_s		*pmtcurrent;

	GList			*progcbl;
};

#if 0
/* Callback for program pids (not pmt) from the demux */
static void pmt_dvr_cb(void *data, void *arg) {
	struct program_s	*prog=arg;
	GList			*progcbl=g_list_first(prog->progcbl);

	while(progcbl) {
		struct programcb_s *pcb=progcbl->data;

		/* Issue callback - typically a input callback */
		pcb->callback(data, pcb->arg);

		progcbl=g_list_next(progcbl);
	}
}
#endif

#define pmt_prog_gets_pid(prog, pid) \
		((prog)->pidtable[(pid)].cb != NULL)

/*
 * This program wants a specific PID - So register it with the DVR demux. As we
 * might habe multiple streams receiving this program we register all streams
 * with the dvr demux.
 *
 * As we need to leave pid in case the PMT changes we need to remember the DVR demux
 * call back cookies. For this we append the cookies to a GList in the programs pidtable.
 *
 */
static void pmt_prog_join_pid(struct program_s *prog, uint16_t pid, unsigned int type) {
	GList		*pcbl=g_list_first(prog->progcbl);

	while(pcbl) {
		struct programcb_s	*pcb=pcbl->data;
		void			*cbc;

		cbc=dvr_add_pcb(prog->adapter, pid, DVRCB_TS, type, pcb->callback, pcb->arg);

		prog->pidtable[pid].cb=g_list_append(prog->pidtable[pid].cb, cbc);

		pcbl=g_list_next(pcbl);
	}
}

/*
 * The programs leaves a certain pid. For this we walk the DVR callback cookie
 * list in the programs pidtable and pass them onto the dvr_del_pcb.
 * If we removed all streams from the dvr callback we delete the list and
 * set the GList pointer in the pidtable to NULL which is the signal that
 * the program is not receiving that pid.
 */
static void pmt_prog_leave_pid(struct program_s *prog, uint16_t pid) {
	GList		*cbcl=g_list_first(prog->pidtable[pid].cb);

	while(cbcl) {
		void			*cbc=cbcl->data;
		dvr_del_pcb(prog->adapter, pid, cbc);
		cbcl=g_list_next(cbcl);
	}

	g_list_free(prog->pidtable[pid].cb);
	prog->pidtable[pid].cb=NULL;
}

static uint16_t pmt_pnr(struct psisec_s *section) {
	return (section->data[PMT_PNR_OFF1] << 8 | section->data[PMT_PNR_OFF2]);
}

static int pmt_mapstreamtype(uint8_t type) {
	switch (type) {
		case 1:		/* ISO/IEC 11172 Video		*/
		case 2:		/* ITU-T Rec. H.262		*/
				/* ISO/IEC 13818-2 Video	*/
				/* ISO/IEC 11172-2		*/
		case 27:	/* ITU-T Rec. H.264		*/
				/* ISO/IEC 14496-10 Video	*/
			return PID_VIDEO;
			break;
		case 3:		/* ISO/IEC 11172 Audio */
		case 4:		/* ISO/IEC 13818-3 Audio */
			return PID_AUDIO;
			break;
		case 5:		/* ISO/IEC 13818-1 Page 160 - Private Sections */
		case 6:		/* ITU-T Rec. H.222.0 ISO/IEC 13818-1 PES - Private Data */
				/* Stephen Gardner sent dvbsnoop output showing AC3 Audio
				 * in here encapsulated in the private data. As we dont
				 * treat different pid types differently we don't care for
				 * now						*/
			return PID_PRIVATE;
			break;
		case 7:	 /* ISO/IEC 13522 MHEG */
		case 8:  /* ITU-T Rec. H.220.0 / ISO/IEC 13818-1 Annex A DSM CC */
		case 9:	 /* ITU-T Rec. H.220.1 */
		case 10: /* ISO/IEC 13818-6 Type A */
		case 11: /* ISO/IEC 13818-6 Type B */
		case 12: /* ISO/IEC 13818-6 Type C */
		case 13: /* ISO/IEC 13818-6 Type D */
		case 14: /* ISO/IEC 13818-1 auxiliary */
			return PID_OTHER;
			break;
		default:
			if (type & 0x80)
				return PID_USER;
	}
	return PID_OTHER;
}

static uint16_t pmt_pcr(struct psisec_s *section) {
	return (section->data[PMT_PCR_OFF1] << 8 | section->data[PMT_PCR_OFF2]) & PID_MASK;
}

static unsigned int pmt_pinfo_len(struct psisec_s *section) {
	return (section->data[PMT_PILEN_OFF1] << 8 | section->data[PMT_PILEN_OFF2]) & PMT_PILEN_MASK;
}

#define ESINFO_LEN_OFF	3
#define ESINFO_LEN_MASK	0x0fff
#define ESINFO_MIN_LEN	5

static int pmt_es_infolen(struct psisec_s *section, int es) {
	return (section->data[es+ESINFO_LEN_OFF]<<8 |
			section->data[es+ESINFO_LEN_OFF+1]) & ESINFO_LEN_MASK;
}

static int pmt_next_es(struct psisec_s *section, int es) {
	int		nes;
	nes=es+pmt_es_infolen(section, es)+ESINFO_MIN_LEN;

	/* Next offset beyond PMT end ? */
	if (nes >= psi_len(section)-CRC32_LEN)
		return 0;

	return nes;
}

static int pmt_first_es(struct psisec_s *section) {
	return PMT_PI_OFF+pmt_pinfo_len(section);
}

static uint8_t pmt_es_type(struct psisec_s *section, int es) {
	return section->data[es];
}

#define ES_PID_OFF	1
static unsigned int pmt_es_pid(struct psisec_s *section, int es) {
	return ((section->data[es+ES_PID_OFF]<<8 | section->data[es+ES_PID_OFF+1]) & PID_MASK);
}

unsigned int pmt_get_pmtpid(void *pvoid) {
	struct program_s	*prog=pvoid;
	return prog->pmtpid;
}

struct pmt_s *pmt_new(void ) {
	return calloc(1, sizeof(struct pmt_s));
}

void pmt_free(struct pmt_s *pmt) {
	GList	*es=g_list_first(pmt->es);

	while(es) {
		g_free(es->data);
		es=g_list_next(es);
	}
	g_list_free(pmt->es);

	free(pmt);
}

void pmt_add_es(struct pmt_s *pmt, uint8_t type, uint16_t pid) {
	struct pmtes_s	*es=g_new(struct pmtes_s, 1);
	es->streamtype=type;
	es->espid=pid;
	pmt->es=g_list_append(pmt->es, es);
}

static struct pmt_s *pmt_parse(struct program_s *prog) {
	struct pmt_s		*pmt;
	struct psisec_s		*s;
	int			lastsecnum;
	int			secnum;
	int			esoff;
	int			es=0;

	if (!prog->psi.section[0])
		return NULL;

	pmt=pmt_new();

	lastsecnum=psi_last_section_number(prog->psi.section[0]);

	for(secnum=0;secnum<=lastsecnum;secnum++) {
		s=prog->psi.section[secnum];

		if (!s)
			continue;

		/*
		 * FIXME - Multiple section PMTs might be inconsistent
		 * concerning the PCR pid
		 */
		if (pmt_pcr(s) != PID_MAX)
			pmt->pcrpid=pmt_pcr(s);

		/* Walk PMT and add ES PIDS as necessary */
		esoff=pmt_first_es(s);
		do {
			uint8_t		type=pmt_es_type(s, esoff);
			uint16_t	pid=pmt_es_pid(s, esoff);

			pmt_add_es(pmt, type, pid);
			es++;

		} while((esoff=pmt_next_es(s, esoff)) != 0);
	}

	logwrite(LOG_DEBUG, "pmt: parse_pmt found %d elementary streams for pnr %04x", es, prog->pnr);

	return pmt;
}

static struct pmtes_s *pmt_find_es(struct pmt_s *pmt, uint16_t pid) {
	GList		*esl;
	struct pmtes_s	*es;

	for(esl=g_list_first(pmt->es);esl;esl=g_list_next(esl)) {
		es=esl->data;
		if (es->espid == pid)
			return es;
	}

	return NULL;
}

static void pmt_update(struct program_s *prog) {
	struct pmt_s		*current=prog->pmtcurrent;
	struct pmt_s		*last=prog->pmtlast;
	GList			*esl;
	struct pmtes_s		*escur, *eslast;
	int			i;

	logwrite(LOG_DEBUG, "pmt: pmt_update running for program %04x", prog->pnr);

	for(esl=g_list_first(current->es);esl;esl=g_list_next(esl)) {
		escur=esl->data;

		if (last) {
			eslast=pmt_find_es(last, escur->espid);
			if (eslast)
				continue;
		}

		if (!pmt_prog_gets_pid(prog, escur->espid))
			pmt_prog_join_pid(prog, escur->espid, pmt_mapstreamtype(escur->streamtype));
	}

	/*
	 * We check if we get the PCR pid already. It seems most programs
	 * multiplex the PCR informations into the Audio PID instead
	 * a seperate PID so there would be no need to add a seperate callback
	 *
	 */
	if (current->pcrpid != PID_MAX) {
		if (!pmt_prog_gets_pid(prog, current->pcrpid))
			pmt_prog_join_pid(prog, current->pcrpid, PID_PCR);
	}

	/*
	 * Walk all joined pids and check whether they are still active.
	 * If they are not active - leave
	 */
	for(i=0;i<PID_MAX;i++) {
		if (pmt_prog_gets_pid(prog, i)) {

			if (current->pcrpid == i)
				continue;

			if (prog->pmtpid == i)
				continue;

			if (pmt_find_es(current, i))
				continue;

			pmt_prog_leave_pid(prog, i);
		}
	}
}

static void pmt_dvr_pmt_cb(void *data, void *arg) {
	struct psisec_s		*section=data;
	struct program_s	*prog=arg;
	struct pmt_s		*pmt;

	logwrite(LOG_XTREME, "pmt: dvr section callback");
	dump_hex(LOG_XTREME, "pmt: PMT ", section->data, section->valid);

	if (!psi_currentnext(section))
		return;

	if (psi_tableid(section) != PMT_TABLE_ID) {
		logwrite(LOG_INFO, "pmt: received PMT with broken table id on pid %d", prog->pmtpid);
		return;
	}

	if (pmt_pnr(section) != prog->pnr) {
		logwrite(LOG_DEBUG, "pmt: received PMT section for pnr %04x expected %04x",
				pmt_pnr(section), prog->pnr);
		return;
	}

	if (!psi_update_table(&prog->psi, section))
		return;

	pmt=pmt_parse(prog);

	if (prog->pmtlast)
		pmt_free(prog->pmtlast);

	prog->pmtlast=prog->pmtcurrent;
	prog->pmtcurrent=pmt;

	pmt_update(prog);
}

/* Find program structure for a given PNR (Program Number) */
static struct program_s *pmt_prog_from_pnr(struct adapter_s *a, unsigned int pnr) {
	struct program_s	*prog;
	GList			*pl=g_list_first(a->pmt.pnrlist);

	/* Find pnr struct for this pnr */
	while(pl) {
		prog=pl->data;
		if (prog->pnr == pnr)
			return prog;
		pl=g_list_next(pl);
	}

	return NULL;
}

/*
 * Callback from PAT parsing - Passes in adapter, pnr and pmtpid - If
 * we want this PNR and we didnt already join the PMT do so - In case
 * of a PMT pid change - leave old pid and join new one.
 *
 * In case of a PMTPID of 0 the PAT did not contain our program anymore.
 *
 */

void pmt_pidfrompat(struct adapter_s *a, unsigned int pnr, unsigned int pmtpid) {
	struct program_s	*prog;

	prog=pmt_prog_from_pnr(a, pnr);

	if (!prog)
		return;

	logwrite(LOG_XTREME, "pmt: pidfrompat found pnr %04x", pnr);

	if (prog->pmtpid == pmtpid)
		return;

	logwrite(LOG_XTREME, "pmt: pidfrompat pmt pid changed from %04x to %04x", 
				prog->pmtpid, pmtpid);

	/* If we have an old one - leave it */
	if (prog->pmtpid) {
		dvr_del_pcb(a, prog->pmtpid, prog->pmtpidcb);
		pmt_prog_leave_pid(prog, prog->pmtpid);
	}

	if (pmtpid) {
		/* Add a callback for the new pmt pid */
		prog->pmtpidcb=dvr_add_pcb(a, pmtpid, DVRCB_SECTION, PID_PMT, pmt_dvr_pmt_cb, prog);
		pmt_prog_join_pid(prog, pmtpid, PID_PMT);
	} else {
		/* FIXME - The Program disappeared - we should leave all pids */
	}

	prog->pmtpid=pmtpid;
}

static struct program_s *pmt_prog_new(struct adapter_s *a, unsigned int pnr) {
	struct program_s	*prog;

	prog=calloc(1, sizeof(struct program_s));

	if (!prog)
		return NULL;

	prog->pnr=pnr;
	prog->pmtpid=0;
	prog->adapter=a;

	a->pmt.pnrlist=g_list_append(a->pmt.pnrlist, prog);

	return prog;
}

static void pmt_prog_add_cb(struct program_s *prog,
			void (*callback)(void *data, void *arg), void *arg) {
	struct programcb_s	*pcb;

	pcb=malloc(sizeof(struct programcb_s));
	pcb->callback=callback;
	pcb->arg=arg;

	prog->progcbl=g_list_append(prog->progcbl, pcb);
}

/*
 * Called from input_pnr to join a program number by supplying an
 * callback which will be put into the demux table by the PMT parser
 *
 */
void *pmt_join_pnr(struct adapter_s *a, unsigned int pnr,
			void (*callback)(void *data, void *arg), void *arg) {
	struct program_s	*prog;

	/*
	 * Find this program - might be multiple output
	 * streams for the same program
	 */
	prog=pmt_prog_from_pnr(a, pnr);

	/* If we havent got one - create it */
	if (!prog)
		prog=pmt_prog_new(a, pnr);

	/* Add callback to this stream to program list */
	if (prog)
		pmt_prog_add_cb(prog, callback, arg);

	pat_init(a);

	return prog;
}
