
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "getstream.h"
#include "psi.h"
#include "crc32.h"

/*
 * PAT Programm Association Table
 *
 * The PAT is multiplexed into the TS (Transport Stream)
 * at PID 0x0. It contains the table of all PMT (Program Map Table)
 * pids within the TS.
 *
 * Name				Bits	Offset
 *
 * table_id			 8	 0
 * section_syntax_indicator	 1	 1
 * pad (0)			 1	 1
 * reserved			 2	 1
 * section_length		12	 1/2
 * transport_stream_id		16	 3/4
 * reserved			 2	 5
 * version_number		 5	 5
 * current_next_indicator	 1	 5
 * section_number		 8	 6
 * last_section_number		 8	 7
 *
 * 1..N
 *   program_number		16
 *   reserved			 3
 *   pid			13
 *      (if pnum==0 network_pid else program_map_pid)
 *
 * crc32			32
 *
 */

struct patprog_s {
	uint16_t	pnr;
	uint16_t	pid;
};

static int pat_pnrno(struct psisec_s *section) {
	return psi_payload_len(section)/PAT_PNR_LEN;
}
static int pat_pnrfrompat(struct psisec_s *section, int i) {
	uint8_t	*pat=section->data;
	return pat[PAT_HDR_LEN+i*4+0] << 8 | pat[PAT_HDR_LEN+i*4+1];
}
static int pat_pidfrompat(struct psisec_s *section, int i) {
	uint8_t	*pat=section->data;
	return (pat[PAT_HDR_LEN+i*4+2] << 8 | pat[PAT_HDR_LEN+i*4+3]) & PID_MASK;
}


/*
 * Send out a pat created from a struct pat_s. Things like the CC (Continuity Counter)
 * version and transport id are passed from the caller.
 * pat_send assembles the sections and passes them onto psi_segment_and_send which
 * will create TS packets and feed them into the callback.
 *
 * FIXME This is completely untested with multisegment PATs so beware of the dogs.
 *
 *
 */
unsigned int pat_send(struct pat_s *pat, uint8_t cc, uint8_t version, uint16_t tid,
		void (*callback)(void *data, void *arg), void *arg) {

	struct psisec_s	*section=psi_section_new();
	uint8_t		*p;
	uint32_t	ccrc;
	GList		*pl=g_list_first(pat->program);
	int		i, seclen, patlen;
	int		pkts=0;

	/* FIXME - Need to calculate number of sections */

	p=section->data;

	while(1) {
		memset(p, 0xff, PSI_SECTION_MAX);
		p[PSI_TABLE_ID_OFF]=PAT_TABLE_ID;
		p[PAT_TID_OFF1]=(tid>>8);
		p[PAT_TID_OFF2]=(tid&0xff);

		/* Version and set lowest bit to 1 (current next) */
		p[PSI_VERSION_OFF]=(version&0x1f)<<1|1;
		p[PSI_SECNO_OFF]=0x0;
		p[PSI_LASTSECNO_OFF]=0x0;

		i=0;
		for(;pl;pl=g_list_next(pl)) {
			struct patprog_s *pp=pl->data;

			p[PAT_HDR_LEN+i*4+0]=pp->pnr >> 8;
			p[PAT_HDR_LEN+i*4+1]=pp->pnr & 0xff;

			p[PAT_HDR_LEN+i*4+2]=pp->pid >> 8;
			p[PAT_HDR_LEN+i*4+3]=pp->pid & 0xff;

			/* FIXME - Should check for PSI Section overflow (multi section PAT) */
			i++;
		}

		patlen=PAT_HDR_LEN+i*4+CRC32_LEN;
		seclen=(patlen-PSI_SECLEN_ADD)&PSI_SECLEN_MASK;

		p[PSI_SECLEN_OFF+0]=0x80|(seclen>>8);
		p[PSI_SECLEN_OFF+1]=(seclen&0xff);

		ccrc=crc32_be(0xffffffff, p, patlen-CRC32_LEN);

		p[patlen-CRC32_LEN+0]=(ccrc>>24)&0xff;
		p[patlen-CRC32_LEN+1]=(ccrc>>16)&0xff;
		p[patlen-CRC32_LEN+2]=(ccrc>>8)&0xff;
		p[patlen-CRC32_LEN+3]=(ccrc&0xff);

		pkts=psi_segment_and_send(section, 0, cc+pkts, callback, arg);

		/* If we have all programs in this section */
		if (!pl)
			break;
	}

	psi_section_free(section);

	return pkts;
}

struct pat_s *pat_new() {
	return calloc(1, sizeof(struct pat_s));
}

void pat_free(struct pat_s *pat) {
	GList	*pl=g_list_first(pat->program);
	while(pl) {
		g_free(pl->data);
		pl=g_list_next(pl);
	}
	g_list_free(pat->program);
	free(pat);
}

void pat_add_program(struct pat_s *pat, uint16_t pnr, uint16_t pid) {
	struct patprog_s	*prog=g_new(struct patprog_s, 1);

	prog->pnr=pnr;
	prog->pid=pid;

	pat->program=g_list_append(pat->program, prog);
}

struct pat_s *pat_parse(struct adapter_s *a) {
	struct psisec_s	*s;
	struct pat_s	*next;
	unsigned int	secnum;
	unsigned int	lastsecnum;
	unsigned int	pnr;

	if (!a->pat.psi.section[0])
		return NULL;

	next=pat_new();

	lastsecnum=psi_last_section_number(a->pat.psi.section[0]);

	for(secnum=0;secnum<=lastsecnum;secnum++) {
		s=a->pat.psi.section[secnum];

		if (!s)
			continue;

		for(pnr=0;pnr<=pat_pnrno(s);pnr++) {

			pat_add_program(next,
					pat_pnrfrompat(s, pnr),
					pat_pidfrompat(s, pnr));

			next->progcount++;
		}
	}

	logwrite(LOG_DEBUG, "pat: pat parse found %d programs", next->progcount);

	return next;
}

static struct patprog_s *pat_findpnr(struct pat_s *pat, uint16_t pnr) {
	GList	*ppl=g_list_first(pat->program);
	while(ppl) {
		struct patprog_s *prog=ppl->data;
		if (prog->pnr == pnr)
			return prog;
		ppl=g_list_next(ppl);
	}
	return NULL;
}

/*
 * The PAT did change e.g. we added or changed a section
 * Parse it into our pat_s structure and check which PMTs
 * changed or were added and service the PMT callback
 *
 */
static void pat_update(struct adapter_s *a) {
	struct pat_s		*current=a->pat.current;
	struct pat_s		*last=a->pat.last;
	GList			*pl;
	struct patprog_s	*plast, *pcur;

	/* Check whether new programs appeared or the programs PMTPIDs changed */
	for(pl=g_list_first(current->program);pl;pl=g_list_next(pl)) {
		pcur=pl->data;

		if (pcur->pid == 0) {
			logwrite(LOG_ERROR, "pat: Invalid PMT pid 0 for pnr %d", pcur->pnr);
			continue;
		}

		/* Do we have a current PAT ? */
		if (!last) {
			pmt_pidfrompat(a, pcur->pnr, pcur->pid);
		} else {
			/* Do we have the program and if so did the PMT pid change ? */
			plast=pat_findpnr(last, pcur->pnr);

			if (plast && plast->pid != pcur->pid)
				pmt_pidfrompat(a, pcur->pnr, pcur->pid);
		}
	}

	if (!last)
		return;

	/* Did programs disappear e.g. we dont have their PNR anymore */
	for(pl=g_list_first(last->program);pl;pl=g_list_next(pl)) {
		plast=pl->data;

		pcur=pat_findpnr(current, plast->pnr);

		if (!pcur)
			pmt_pidfrompat(a, plast->pnr, 0);
	}
}

/* Add/Update a section in our PAT.
 *
 * - Check whether this is a current section
 * - Check whether its really a PAT
 * - Add/Update the section in our PSI table
 * - If we had an update/change parse the PAT and call pat_update
 *
 * Input:
 *	- pointer to our adapter
 *	- a static section (need to clone before usage)
 *
 */
static void pat_section_cb(void *data, void *arg) {
	struct psisec_s		*section=data;
	struct adapter_s	*adapter=arg;

	if (!psi_currentnext(section))
		return;

	if (psi_tableid(section) != PAT_TABLE_ID)
		return;

	if (!psi_update_table(&adapter->pat.psi, section))
		return;

	if (adapter->pat.last)
		pat_free(adapter->pat.last);

	adapter->pat.last=adapter->pat.current;
	adapter->pat.current=pat_parse(adapter);

	pat_update(adapter);

	return;
}

void pat_init(struct adapter_s *adapter) {

	/* Did we already add a callback? */
	if (adapter->pat.cbc != NULL)
		return;

	adapter->pat.cbc=dvr_add_pcb(adapter,
				0, DVRCB_SECTION, PID_PAT,
				pat_section_cb, adapter);
}
