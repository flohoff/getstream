
#include "getstream.h"
#include "psi.h"

static void sdt_section_cb(void *data, void *arg) {

	struct psisec_s		*section=data;
	/*struct adapter_s	*adapter=arg;*/

	if (psi_tableid(section) != SDT_TABLE_ID)
		return

	logwrite(LOG_INFO, "sdt: Received sdt");
	dump_hex(LOG_XTREME, "sdt: ", section->data, section->valid);
}

void sdt_init(struct adapter_s *adapter) {

	adapter->sdt.cbc=dvr_add_pcb(adapter,
				0x11, DVRCB_SECTION, PID_TYPE_SDT,
				sdt_section_cb, adapter);

}
