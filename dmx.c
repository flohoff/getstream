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

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include <event.h>

#include "getstream.h"
#include "psi.h"

static inline char *dmxname(int adapter) {
	static char	dmxname[128];

	sprintf(dmxname, "/dev/dvb/adapter%d/demux0", adapter);

	return dmxname;
}

/*
 * DMX_PES_VIDEO
 * DMX_PES_AUDIO
 * DMX_PES_TELETEXT
 * DMX_PES_OTHER
 *
 */
static int dmx_set_pes_filter(int fd, int pid, int pestype) {
	struct dmx_pes_filter_params pesFilterParams;

	memset(&pesFilterParams, 0, sizeof(struct dmx_pes_filter_params));

	logwrite(LOG_INFO,"dmx: Setting filter for pid %d pestype %d", pid, pestype);

	pesFilterParams.pid		= pid;
	pesFilterParams.input		= DMX_IN_FRONTEND;
	pesFilterParams.output		= DMX_OUT_TS_TAP;
	pesFilterParams.pes_type	= pestype;
	pesFilterParams.flags		= DMX_IMMEDIATE_START;

	if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)  {
		logwrite(LOG_ERROR,"demux: ioctl DMX_SET_PES_FILTER failed for pid %u pestype %d ",pid, pestype);
		return 0;
	}

	return 1;
}

void dmx_leave_pid(struct adapter_s *a, int pid) {
	if (a->dmx.pidtable[pid].fd >= 0)
		close(a->dmx.pidtable[pid].fd);
	a->dmx.pidtable[pid].fd=-1;
	return;
}

int dmx_join_pid(struct adapter_s *a, unsigned int pid, int type) {
	int	fd;

	/* Budget mode does not need this */
	if(a->budgetmode && a->dmx.pidtable[0x2000].fd >= 0)
		return 1;

	/* Already joined ? */
	if (a->dmx.pidtable[pid].fd >= 0) {
		logwrite(LOG_ERROR,"dmx: already joined pid %d", pid);
		return 1;
	}

	fd=open(dmxname(a->no), O_RDWR);

	if (fd < 0) {
		logwrite(LOG_ERROR,"dmx: failed opening dmx device for joining pid %d", pid);
		return 0;
	}

	if (!dmx_set_pes_filter(fd, pid, type)) {
		close(fd);
		return 0;
	}

	a->dmx.pidtable[pid].fd=fd;
	a->dmx.pidtable[pid].type=type;

	return 1;
}

/*
 * Apply section filter to opened demux interface.
 *
 */
int demux_set_sct_filter(int fd, int pid,
		struct dmx_filter *df, int flags, int timeout) {

	struct dmx_sct_filter_params	sctFilterParams;

	memset(&sctFilterParams, 0, sizeof(struct dmx_sct_filter_params));

	sctFilterParams.pid=pid;
	sctFilterParams.timeout=timeout;
	sctFilterParams.flags=flags;

	memcpy(&sctFilterParams.filter, df, sizeof(struct dmx_filter));

	if (ioctl(fd, DMX_SET_FILTER, &sctFilterParams) < 0) {
		logwrite(LOG_ERROR, "demux: ioctl DMX_SET_PES_FILTER failed for pid %u",pid);
		exit(-1);
	}

	return 0;
}

/*
 * It is known that the flexcop chipset aka SkyStar2/AirStar cards
 * sometimes stop receiving interrupts. A workaround in the kernel
 * trys to reset the card in case the number of joined/forwarded
 * pids gets from 0 to 1 which means we need to drop all filters
 * and reaquire them. This function is called from dvr.c in case
 * we see no read avalability on the dvr0 device in 5 seconds.
 *
 * We are going up to PID_MAX+1 aka 0x2000 in case we are in budget
 * mode and just have a single filter.
 *
 */
void dmx_bounce_filter(struct adapter_s *adapter) {
	int		i;

	for(i=0;i<=PID_MAX+1;i++) {
		if (adapter->dmx.pidtable[i].fd < 0)
			continue;

		close(adapter->dmx.pidtable[i].fd);
	}

	for(i=0;i<=PID_MAX+1;i++) {
		if (adapter->dmx.pidtable[i].fd < 0)
			continue;

		adapter->dmx.pidtable[i].fd=-1;

		dmx_join_pid(adapter, i, adapter->dmx.pidtable[i].type);
	}

}

int dmx_init(struct adapter_s *adapter) {
	int		i;

	/* Reset dmxfd fds - Run until 0x2000 as thats the budget mode pid */
	for(i=0;i<=PID_MAX+1;i++)
		adapter->dmx.pidtable[i].fd=-1;

	return 1;
}
