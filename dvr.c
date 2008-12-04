#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include <assert.h>

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include <event.h>

#include "getstream.h"

const char       *pidtnames[]={ "None", "PMT", "PCR", "Video", "Audio", "Privat", "User", "Static", "Other" };

struct pidcallback_s {
	void		(*callback)(void *data, void *arg);
	void		*arg;
	uint16_t	pid;
	unsigned int	type;
	unsigned int	pidt;
};

static inline char *dvrname(int adapter) {
	static char	dvrname[128];

	sprintf(dvrname, "/dev/dvb/adapter%d/dvr0", adapter);

	return dvrname;
}

static inline void dvr_input_ts(struct adapter_s *a, uint8_t *ts) {
	uint16_t	pid;
	GList		*pcbl;

	/* TS (Transport Stream) packets start with 0x47 */
	if (ts[TS_SYNC_OFF] != TS_SYNC) {
		logwrite(LOG_XTREME, "dvr: Non TS Stream packet (!0x47) received on dvr0");
		dump_hex(LOG_XTREME, "dvr:", ts, TS_PACKET_SIZE);
		return;
	}

	pid=ts_pid(ts);

	a->pidtable[pid].packets++;

	/* Does somebody want this pid ? */
	if (!a->pidtable[pid].callback)
		return;

	/* FIXME This is ugly to have the same list with different users */
	if (a->pidtable[pid].secuser) {
		int	off=0;
		while(1) {
			off=psi_reassemble(a->pidtable[pid].section, ts, off);

			if (off<0)
				break;

			for(pcbl=g_list_first(a->pidtable[pid].callback);pcbl!=NULL;pcbl=g_list_next(pcbl)) {
				struct pidcallback_s	*pcb=pcbl->data;
				if (pcb->type == DVRCB_SECTION)
					pcb->callback(a->pidtable[pid].section, pcb->arg);
			}
		}
	}

	for(pcbl=g_list_first(a->pidtable[pid].callback);pcbl!=NULL;pcbl=g_list_next(pcbl)) {
		struct pidcallback_s	*pcb=pcbl->data;
		if (pcb->type == DVRCB_TS)
			pcb->callback(ts, pcb->arg);
	}
}

void dvr_del_pcb(struct adapter_s *a, unsigned int pid, void *vpcb) {
	struct pidcallback_s	*pcb=vpcb;

	logwrite(LOG_DEBUG, "dvr: Del callback for PID %4d (0x%04x) type %d (%s)",
			pid, pid, pcb->pidt, pidtnames[pcb->pidt]);

	a->pidtable[pid].callback=g_list_remove(a->pidtable[pid].callback, pcb);

	if (pcb->type == DVRCB_SECTION) {
		a->pidtable[pid].secuser--;
		if (!a->pidtable[pid].secuser)
			psi_section_free(a->pidtable[pid].section);
	}

	free(pcb);

	if (!a->pidtable[pid].callback)
		dmx_leave_pid(a, pid);
}

void *dvr_add_pcb(struct adapter_s *a, unsigned int pid, unsigned int type,
		unsigned int pidt, void (*callback)(void *data, void *arg), void *arg) {
	struct pidcallback_s	*pcb;

	logwrite(LOG_DEBUG, "dvr: Add %s callback for PID %4d (0x%04x) type %d (%s)",
			((type == DVRCB_TS) ? "TS" : "Section"), pid, pid, pidt, pidtnames[pidt]);

	/* FIXME - check for error of malloc */
	pcb=malloc(sizeof(struct pidcallback_s));

	pcb->pid=pid;
	pcb->callback=callback;
	pcb->arg=arg;
	pcb->pidt=pidt;
	pcb->type=type;

	if (type == DVRCB_SECTION) {
		a->pidtable[pid].secuser++;
		if (!a->pidtable[pid].section)
			a->pidtable[pid].section=psi_section_new();
	}

	if (!a->pidtable[pid].callback)
		dmx_join_pid(a, pid, DMX_PES_OTHER);

	a->pidtable[pid].callback=g_list_append(a->pidtable[pid].callback, pcb);

	return pcb;
}

/*
 * Read TS packet chunks from dvr0 device and fill them one after
 * another into dvr_input_ts
 */
static void dvr_read(int fd, short event, void *arg) {
	int			len, i;
	struct adapter_s	*adapter=arg;
	uint8_t			*db=adapter->dvrbuf;

	do {
		len=read(fd, db, adapter->dvrbufsize*TS_PACKET_SIZE);

		adapter->reads++;

		/* EOF aka no more TS Packets ? */
		if (len == 0)
			break;

		/* Read returned error ? */
		if (len < 0) {
			if (errno != EAGAIN)
				logwrite(LOG_ERROR, "demux: read in dvr_read returned with errno %d", errno);
			break;
		}
#if 0
		/* Never ever happened so we remove this - its the hot path */
		/* We should only get n*TS_PACKET_SIZE */
		if (len % TS_PACKET_SIZE != 0) {
			logwrite(LOG_ERROR, "demux: Oops - unaligned read of %d bytes dropping buffer", len);
			continue;
		}
#endif
		/* Loop on TS packets and fill them into dvr_input_ts */
		for(i=0;i<len;i+=TS_PACKET_SIZE) {
			dvr_input_ts(adapter, &db[i]);
		}

	} while (len == adapter->dvrbufsize*TS_PACKET_SIZE);
}


void dvr_init_stat_timer(struct adapter_s *a);

#define DVR_STAT_INTERVAL	60

static void dvr_stat_timer(int fd, short event, void *arg) {
	struct adapter_s	*a=arg;
	int			i;
	unsigned long		total=0, pids=0;

	for(i=0;i<PID_MAX;i++) {
		if (a->pidtable[i].packets) {
			pids++;
			total+=a->pidtable[i].packets;
		}
	}

	logwrite(LOG_INFO, "dvr: inputstats: %d pids %u pkt/s %u byte/s %u read/s",
				pids,
				total/a->statinterval,
				total*TS_PACKET_SIZE/a->statinterval,
				a->reads/a->statinterval);

	for(i=0;i<PID_MAX;i++) {
		a->pidtable[i].packets=0;
	}
	a->reads=0;

	dvr_init_stat_timer(a);
}

void dvr_init_stat_timer(struct adapter_s *a) {
	struct timeval	tv;

	if (!a->statinterval)
		return;

	a->statlast=time(NULL);

	tv.tv_sec=a->statinterval;
	tv.tv_usec=0;

	evtimer_set(&a->statevent, dvr_stat_timer, a);
	evtimer_add(&a->statevent, &tv);
}

int dvr_init(struct adapter_s *a) {
	int			dvrfd;

	a->dvrbuf=malloc(a->dvrbufsize*TS_PACKET_SIZE);

	dvrfd=open(dvrname(a->no), O_RDONLY|O_NONBLOCK);

	if (dvrfd < 0)
		return 0;

	event_set(&a->dvrevent, dvrfd, EV_READ|EV_PERSIST, dvr_read, a);
	event_add(&a->dvrevent, NULL);

	if (a->budgetmode) {
		if (!dmx_join_pid(a, 0x2000, DMX_PES_OTHER)) {
			logwrite(LOG_INFO, "demux: Setting budget filter failed - switching off budget mode");
			a->budgetmode=0;
		}
	}

	a->dvrfd=dvrfd;

	dvr_init_stat_timer(a);

	return 1;
}
