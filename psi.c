#include <string.h>
#include <stdlib.h>

#include "getstream.h"
#include "psi.h"

/*
 * PSI (Program Specific Information) handling
 *
 * Reassembly of TS Packets into sections
 * Handling of multiple sections
 * Callback on change
 *
 *
 *
 */

static uint32_t psi_crc(struct psisec_s *section) {
	uint8_t		*crcp=&section->data[psi_len(section)-4];
	return	crcp[0]<<24|crcp[1]<<16|crcp[2]<<8|crcp[3];
}

/* Calculate the CRC of the section */
static uint32_t psi_ccrc(struct psisec_s *section) {
	return crc32_be(0xffffffff, section->data, psi_len(section)-4);
}

/* Check if PSI has valid CRC32 - return true if it has */
static int psi_crc_valid(struct psisec_s *section) {
	return (psi_crc(section) == psi_ccrc(section));
}

static void psi_section_clear(struct psisec_s *section) {
	section->valid=0;
	section->len=0;
}

int psi_reassemble_continue(struct psisec_s *section, uint8_t *ts, int off) {
	int	copylen;
	uint8_t	ccexp, cc;

	/*
	 * Calculate the next CC counter value. As a section needs to
	 * be completed before the next may begin on a PID we only
	 * accept continuous packets. If we fail the CC test we zap
	 * the whole section.
	 *
	 */
	ccexp=(section->cc+1)&TS_CC_MASK;
	cc=ts_cc(ts);

	if (ccexp != cc) {
		psi_section_clear(section);
		return PSI_RC_CCFAIL;
	}

	/*
	 * If we didnt have a hdr - complete it otherwise we dont
	 * even know the length and cant tell whether the section is
	 * complete.
	 */
	if (!section->len) {
		copylen=PSI_HDR_LEN-section->valid;
		memcpy(&section->data[section->valid], &ts[off], copylen);
		section->valid+=copylen;
		section->len=_psi_len(section->data);
		off+=copylen;
	}

	copylen=MIN(TS_PACKET_SIZE-off, section->len-section->valid);
	memcpy(&section->data[section->valid], &ts[off], copylen);
	section->valid+=copylen;

	return off+copylen;
}

/*
 * Copy the start of an PSI packet into our section buffer beginning
 * at offset and fill section->len if possible
 *
 * Return the new offset
 */
int psi_reassemble_start(struct psisec_s *section, uint8_t *ts, int off) {
	uint8_t		*payloadptr=&ts[off];
	int		copylen;

	psi_section_clear(section);

	section->cc=ts_cc(ts);
	section->pid=ts_pid(ts);

	/* Copy until the end of the packet */
	copylen=TS_PACKET_SIZE-off;

	/*
	 * If not we include the PSI header in which case we
	 * can get the real length
	 *
	 */
	if (TS_PACKET_SIZE-off > PSI_HDR_LEN) {
		section->len=_psi_len(payloadptr);
		copylen=MIN(section->len, TS_PACKET_SIZE-off);
	}

	memcpy(section->data, payloadptr, copylen);

	section->valid=copylen;

	return off+copylen;
}

/*
 * We get passed a static allocated psisec structure, a TS packet and an
 * offset into the packet where we need to start looking for sections.
 *
 * Input:
 *	PSI section structute
 *	TS packet
 *	Offset to start looking for PSI data
 *
 * Output:
 *	Fills PSI section as far as possible
 *
 * Returns:
 *		0	- If section is finished and no more bytes in TS
 *		positive- If section is finished and more bytes in TS
 *		negative- If section is not finished and we are done
 *
 */
/* Reassemble a generic PSI (Program Specific Information) section e.g. PMT PAT CA packet */
int psi_reassemble(struct psisec_s *section, uint8_t *ts, int off) {
	int		noff;
	int		payload;

	if (off)
		payload=off;
	else {
		if (ts_tei(ts))
			return PSI_RC_TEI;

		if (!ts_has_payload(ts))
			return PSI_RC_NOPAYLOAD;

		payload=ts_payload_start(ts);
	}

	if (!off) {
		/*
		 * If "Payload Unit Start Indicator" is set the first byte
		 * payload is the pointer to the first section.
		 * ISO 13818-1 2.4.4.2
		 *
		 */
		if (ts_pusi(ts)) {
			payload+=ts[payload]+1;
			noff=psi_reassemble_start(section, ts, payload);
		} else {
			noff=psi_reassemble_continue(section, ts, payload);
		}
	} else {
		/*
		 * If we are looking for a second section (off != 0) and
		 * the byte after the section is 0xff the TS packet
		 * must be filled with 0xff until the end. ISO 13818-1 2.4.4.
		 * 0xff is a disallowed table_id.
		 *
		 */
		if (ts[off] == 0xff)
			return PSI_RC_NOPAYLOAD;

		noff=psi_reassemble_start(section, ts, payload);
	}

	/* We didnt finish this section in this packet so wait for the next packet */
	if (section->len != section->valid)
		return PSI_RC_INCOMPLETE;

	if (!psi_crc_valid(section))
		return PSI_RC_CRCFAIL;

	return (noff >= TS_PACKET_SIZE) ? 0 : noff;
}

int psi_section_valid(unsigned int pid, struct psisec_s *section, int len) {
	section->pid=pid;
	section->len=len;
	section->valid=0;

	if (!psi_crc_valid(section))
		return PSI_RC_CRCFAIL;

	if (psi_len(section)!=len)
		return PSI_RC_LENFAIL;

	return PSI_RC_OK;
}

struct psisec_s *psi_section_new(void ) {
	return calloc(1, sizeof(struct psisec_s));
}

void psi_section_free(struct psisec_s *section) {
	free(section);
}

struct psisec_s *psi_section_clone(struct psisec_s *section) {
	struct psisec_s *new=psi_section_new();
	memcpy(new, section, sizeof(struct psisec_s));
	return new;
}

int psi_section_fromdata(struct psisec_s *section, unsigned int pid, uint8_t *data, int len) {
	psi_section_clear(section);

	memcpy(&section->data, data, len);

	return psi_section_valid(pid, section, len);
}

unsigned int psi_segment_and_send(struct psisec_s *section, unsigned int pid, uint8_t cc,
			void (*callback)(void *data, void *arg), void *arg) {

	uint8_t		ts[TS_PACKET_SIZE];
	int		pkts=0;
	int		plen=psi_len(section);
	int		left=plen;
	int		tspayloadoff;
	int		copylen;

	while(1) {
		memset(&ts, 0xff, TS_PACKET_SIZE);

		ts[TS_SYNC_OFF]=TS_SYNC;

		ts[TS_PID_OFF1]=pid>>8;
		ts[TS_PID_OFF2]=pid&0xff;

		ts[TS_AFC_OFF]=0x1<<TS_AFC_SHIFT;		/* Payload only */
		ts[TS_CC_OFF]|=(cc+pkts)&TS_CC_MASK;		/* Continuity Counter */

		tspayloadoff=TS_PAYLOAD_OFF;
		if (!pkts) {
			ts[TS_PID_OFF1]|=TS_PUSI_MASK;
			ts[TS_PAYLOAD_OFF]=0x0;			/* Clear PSI Pointer */
			tspayloadoff++;
		}

		/* Either full section or as much as fits */
		copylen=MIN(left, TS_PACKET_SIZE-tspayloadoff);

		/* Copy PSI section into TS packet */
		memcpy(&ts[tspayloadoff], &section->data[plen-left], copylen);

		callback(ts, arg);

		left-=copylen;

		pkts++;

		if (left <= 0)
			break;
	}

	return pkts;
}

int psi_update_table(struct psi_s *psi, struct psisec_s *section) {
	uint8_t		secnum;
	uint8_t		version;

	secnum=psi_section_number(section);
	version=psi_version(section);

	/* Check if we have this section or if the section version changed */
	if (psi->section[secnum]) {
		if (version == psi_version(psi->section[secnum]))
			return 0;
		psi_section_free(psi->section[secnum]);
	}

	psi->section[secnum]=psi_section_clone(section);

	return 1;
}
