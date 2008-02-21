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

const char       *pidtnames[]={ "None", "PAT", "PMT", "PCR", "Video", "Audio", "Privat", "User", "Static", "Other" };

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

	/* Full stream callbacks - pseudo pid 0x2000 */
	for(pcbl=g_list_first(a->dvr.fullcb);pcbl!=NULL;pcbl=g_list_next(pcbl)) {
		struct pidcallback_s	*pcb=pcbl->data;
		pcb->callback(ts, pcb->arg);
	}

	pid=ts_pid(ts);
	a->dvr.pidtable[pid].packets++;

	/* Does somebody want this pid ? */
	if (!a->dvr.pidtable[pid].callback)
		return;

	/* FIXME This is ugly to have the same list with different users */
	if (a->dvr.pidtable[pid].secuser) {
		int	off=0;
		while(1) {
			off=psi_reassemble(a->dvr.pidtable[pid].section, ts, off);

			if (off<0)
				break;

			for(pcbl=g_list_first(a->dvr.pidtable[pid].callback);pcbl!=NULL;pcbl=g_list_next(pcbl)) {
				struct pidcallback_s	*pcb=pcbl->data;
				if (pcb->type == DVRCB_SECTION)
					pcb->callback(a->dvr.pidtable[pid].section, pcb->arg);
			}
		}
	}

	for(pcbl=g_list_first(a->dvr.pidtable[pid].callback);pcbl!=NULL;pcbl=g_list_next(pcbl)) {
		struct pidcallback_s	*pcb=pcbl->data;
		if (pcb->type == DVRCB_TS)
			pcb->callback(ts, pcb->arg);
	}
}

void dvr_del_pcb(struct adapter_s *a, unsigned int pid, void *vpcb) {
	struct pidcallback_s	*pcb=vpcb;

	logwrite(LOG_DEBUG, "dvr: Del callback for PID %4d (0x%04x) type %d (%s)",
			pid, pid, pcb->pidt, pidtnames[pcb->pidt]);

	a->dvr.pidtable[pid].callback=g_list_remove(a->dvr.pidtable[pid].callback, pcb);

	if (pcb->type == DVRCB_SECTION) {
		a->dvr.pidtable[pid].secuser--;
		if (!a->dvr.pidtable[pid].secuser)
			psi_section_free(a->dvr.pidtable[pid].section);
	}

	free(pcb);

	if (!a->dvr.pidtable[pid].callback)
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

	/* Joined pseudo pid 0x2000 e.g. input full */
	if (pid == PID_MAX+1) {
		/* FIXME: How to detect we need to join 0x2000? */
		a->dvr.fullcb=g_list_append(a->dvr.fullcb, pcb);
		return pcb;
	}

	if (type == DVRCB_SECTION) {
		a->dvr.pidtable[pid].secuser++;
		if (!a->dvr.pidtable[pid].section)
			a->dvr.pidtable[pid].section=psi_section_new();
	}

	if (!a->dvr.pidtable[pid].callback)
		dmx_join_pid(a, pid, DMX_PES_OTHER);

	a->dvr.pidtable[pid].callback=g_list_append(a->dvr.pidtable[pid].callback, pcb);

	return pcb;
}

/*
 * Read TS packet chunks from dvr0 device and fill them one after
 * another into dvr_input_ts
 */
static void dvr_read(int fd, short event, void *arg) {
	int			len, i;
	struct adapter_s	*adapter=arg;
	uint8_t			*db=adapter->dvr.buffer.ptr;

	do {
		len=read(fd, db, adapter->dvr.buffer.size*TS_PACKET_SIZE);

		/*
		 * Increase readcounter - we need to detect flexcop not delivering
		 * TS packets anymore e.g. the IRQ Stop bug. A timer will then
		 * try to issue a frontend tune which will hopefully reset the
		 * card to deliver packets
		 *
		 */
		adapter->dvr.stat.reads++;

		/* EOF aka no more TS Packets ? */
		if (len == 0)
			break;

		/* Read returned error ? */
		if (len < 0) {
			if (errno != EAGAIN)
				logwrite(LOG_ERROR, "demux: read in dvr_read returned with errno %d", errno);
			break;
		}

		/* Loop on TS packets and fill them into dvr_input_ts */
		for(i=0;i<len;i+=TS_PACKET_SIZE) {
			dvr_input_ts(adapter, &db[i]);
		}

	} while (len == adapter->dvr.buffer.size*TS_PACKET_SIZE);
}


static void dvr_init_stat_timer(struct adapter_s *a);

#define DVR_STAT_INTERVAL	60

static void dvr_stat_timer(int fd, short event, void *arg) {
	struct adapter_s	*a=arg;
	int			i;
	unsigned long		total=0, pids=0;

	for(i=0;i<PID_MAX;i++) {
		if (a->dvr.pidtable[i].packets) {
			pids++;
			total+=a->dvr.pidtable[i].packets;
		}
	}

	logwrite(LOG_INFO, "dvr: inputstats: %d pids %u pkt/s %u byte/s",
				pids,
				total/a->dvr.stat.interval,
				total*TS_PACKET_SIZE/a->dvr.stat.interval);

	for(i=0;i<PID_MAX;i++) {
		a->dvr.pidtable[i].packets=0;
	}

	dvr_init_stat_timer(a);
}

static void dvr_init_stat_timer(struct adapter_s *a) {
	struct timeval	tv;

	if (!a->dvr.stat.interval)
		return;

	a->dvr.stat.last=time(NULL);

	tv.tv_sec=a->dvr.stat.interval;
	tv.tv_usec=0;

	evtimer_set(&a->dvr.stat.event, dvr_stat_timer, a);
	evtimer_add(&a->dvr.stat.event, &tv);
}

static void dvr_stuck_init(struct adapter_s *a);

static void dvr_stuck_timer(int fd, short event, void *arg) {
	struct adapter_s	*adapter=arg;
	/*
	 * Flexcop is known to randomly lockup. A workaround in the kernel
	 * driver is to reset some registers known to reanimate the flexcop.
	 *
	 * See: http://lkml.org/lkml/2005/6/27/135
	 *
	 * The patch went into 2.6.13-rc3 so in case you are running an FlexCop based
	 * card (SkyStar2, AirStar) you better upgrade to 2.6.13 or better.
	 *
	 * First try was to retune which itself is not enough. One needs to bring
	 * down the number of received pids to 0 as the transition from 0 -> 1 resets
	 * the board. So let dmx bounce all filters.
	 *
	 */
	if (adapter->dvr.stat.reads == 0) {
		logwrite(LOG_ERROR, "dvr: lockup of DVB card detected - trying to reanimate via bouncing filter");
		fe_retune(adapter);
		dmx_bounce_filter(adapter);
	}

	adapter->dvr.stat.reads=0;

	dvr_stuck_init(adapter);
}

static void dvr_stuck_init(struct adapter_s *a) {
	struct timeval	tv;

	/* stuck-interval 0; disables the stuck check timer */
	if (!a->dvr.stuckinterval)
		return;

	tv.tv_sec=a->dvr.stuckinterval/1000;
	tv.tv_usec=a->dvr.stuckinterval%1000*1000;

	evtimer_set(&a->dvr.stucktimer, dvr_stuck_timer, a);
	evtimer_add(&a->dvr.stucktimer, &tv);
}


int dvr_init(struct adapter_s *a) {
	int			dvrfd;

	a->dvr.buffer.ptr=malloc(a->dvr.buffer.size*TS_PACKET_SIZE);

	dvrfd=open(dvrname(a->no), O_RDONLY|O_NONBLOCK);

	if (dvrfd < 0)
		return 0;

	event_set(&a->dvr.dvrevent, dvrfd, EV_READ|EV_PERSIST, dvr_read, a);
	event_add(&a->dvr.dvrevent, NULL);

	if (a->budgetmode) {
		if (!dmx_join_pid(a, 0x2000, DMX_PES_OTHER)) {
			logwrite(LOG_INFO, "demux: Setting budget filter failed - switching off budget mode");
			a->budgetmode=0;
		}
	}

	a->dvr.fd=dvrfd;

	dvr_init_stat_timer(a);
	dvr_stuck_init(a);

	return 1;
}
