#ifndef PSI_H
#define PSI_H

#define PSI_MAX_SIZE		4096		/* ISO 13818-1 says private sections
						   shall not exceed 4093 bytes. Page 91 */

#define PSI_SECLEN_ADD	3		/* Size starts after SECLEN */
#define PSI_SECLEN_OFF	1
#define PSI_SECLEN_MASK	0xfff

#define PSI_VERSION_OFF		5
#define PSI_VERSION_MASK	0x3e
#define PSI_VERSION_SHIFT	1

#define PSI_CURRENTNEXT_OFF	5
#define PSI_CURRENTNEXT_MASK	1

#define PSI_SECNO_OFF		6
#define PSI_LASTSECNO_OFF	7

#define PMT_TABLE_OFF		0
#define PMT_TABLE_ID		0x02
#define PMT_SECLEN_OFF1		1
#define PMT_SECLEN_OFF2		2
#define PMT_SECLEN_MASK		0x0fff
#define PMT_SECNO_OFF		6
#define PMT_LASTSECNO_OFF	7
#define PMT_PNR_OFF1		3
#define PMT_PNR_OFF2		4
#define PMT_PILEN_OFF1		10
#define PMT_PILEN_OFF2		11
#define PMT_PILEN_MASK		0x03ff
#define PMT_PI_OFF		12

#define PMT_PCR_OFF1		8
#define PMT_PCR_MASK1		0x1f
#define PMT_PCR_OFF2		9
#define PMT_PCR_MASK2		0xff

#define PMT_ST_STYPE_OFF	0
#define PMT_ST_PID_OFF1		1
#define PMT_ST_PID_OFF2		2

#define PMT_ST_ESLEN_OFF1	3
#define PMT_ST_ESLEN_OFF2	4

#define PMT_ST_ESLEN_MASK	0x0fff
#define PMT_ST_ES_OFF		5

#define PMT_D_TAG_OFF		0
#define PMT_D_LEN_OFF		1

#define PMT_SECTION_OFF		5
#define PMT_LAST_SECTION_OFF	5

#define PMT_MIN_LEN		(PMT_PI_OFF+CRC32_LEN)

#define PSI_TABLE_ID_OFF	0
#define PSI_TABLE_ID		0x0

#define PAT_SECTION_OFF		6
#define PAT_LAST_SECTION_OFF	7

#define PAT_SLEN_OFF1		1
#define PAT_SLEN_OFF2		2
#define PAT_SLEN_MASK		0x0fff

#define PAT_TID_OFF1		3
#define PAT_TID_OFF2		4

#define PAT_VER_OFF		5

#define PSI_HDR_LEN		8	/* PSI Header of the PAT */
#define PAT_HDR_LEN		8	/* PSI Header of the PAT */
#define PAT_PNR_LEN		4	/* Single PAT entry length in bytes */

#define PAT_MIN_LEN		(PAT_HDR_LEN+PAT_PNR_LEN+CRC32_LEN)

#define PAT_TABLE_ID		0x00

#define PSI_SECTION_MAX		255

#define PSI_RC_OK		0
#define	PSI_RC_TEI		-1
#define PSI_RC_NOPAYLOAD	-2
#define PSI_RC_INCOMPLETE	-3
#define PSI_RC_CRCFAIL		-4
#define PSI_RC_CCFAIL		-5
#define PSI_RC_LENFAIL		-6
#define PSI_RC_CORRUPT		-7


struct psisec_s {
	unsigned int	len;
	unsigned int	valid;
	unsigned int	pid;
	unsigned int	cc;

	uint8_t		data[PSI_MAX_SIZE];
};

struct psi_s {
	struct psisec_s	*section[PSI_SECTION_MAX];
};

/*
 *
 * psi.c
 *
 *
 */

#define _psi_version(data) \
		((data[PSI_VERSION_OFF]&PSI_VERSION_MASK)>>PSI_VERSION_SHIFT)
#define psi_version(section) \
		_psi_version(section->data)

#define _psi_section_number(data) \
		(data[PSI_SECNO_OFF])
#define psi_section_number(section) \
		_psi_section_number(section->data)

#define _psi_last_section_number(data) \
		(data[PSI_LASTSECNO_OFF])
#define psi_last_section_number(section) \
		_psi_last_section_number(section->data)

#define _psi_currentnext(data) \
		(data[PSI_CURRENTNEXT_OFF]&PSI_CURRENTNEXT_MASK)
#define psi_currentnext(section) \
		_psi_currentnext(section->data)

#define _psi_tableid(data) \
		(data[PSI_TABLE_ID_OFF])
#define psi_tableid(section) \
		_psi_tableid(section->data)

#define _psi_len(data) \
		(((data[PSI_SECLEN_OFF]<<8|data[PSI_SECLEN_OFF+1])&PSI_SECLEN_MASK)+PSI_SECLEN_ADD)
#define psi_len(section) \
		_psi_len(section->data)

#define _psi_payload_len(data) \
		(_psi_len(data)-PSI_HDR_LEN-CRC32_LEN)
#define psi_payload_len(section) \
		_psi_payload_len(section->data)

struct psisec_s *psi_section_new(void );
void psi_section_free(struct psisec_s *);
int psi_section_fromdata(struct psisec_s *section, unsigned int pid, uint8_t *data, int len);
int psi_reassemble(struct psisec_s *psi, uint8_t *tsp, int offset);
struct psisec_s *psi_section_clone(struct psisec_s *section);
unsigned int psi_segment_and_send(struct psisec_s *section, unsigned int pid, uint8_t cc,
			void (*callback)(void *data, void *arg), void *arg);
int psi_update_table(struct psi_s *psi, struct psisec_s *section);

#endif
