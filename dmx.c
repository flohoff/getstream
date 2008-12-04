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
	if (a->dmxfd[pid] >= 0)
		close(a->dmxfd[pid]);
	a->dmxfd[pid]=-1;
	return;
}

int dmx_join_pid(struct adapter_s *a, unsigned int pid, int type) {
	int	fd;

	/* Budget mode does not need this */
	if(a->budgetmode && a->dmxfd[0x2000] >= 0)
		return 1;

	/* Already joined ? */
	if (a->dmxfd[pid] >= 0) {
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

	a->dmxfd[pid]=fd;

	return 1;
}


/*
 * This will be called for PAT (Programm Association Table)
 * receive on PID 0. We should parse the PAT and fill the pid
 * demux table with the PMT pids pointing to the PMT decoder.
 *
 */
static void dmx_read(int fd, short event, void *arg) {
	struct adapter_s	*a=arg;
	struct psisec_s		section;
	uint8_t			psi[PSI_MAX_SIZE];
	int			len;

	len=read(fd, &psi, PSI_MAX_SIZE);

	if (len <= 0)
		return;

	logwrite(LOG_XTREME, "dmx: Adapter %d got PAT section from dmx device - len %d",
				a->no, len);

	if (psi_section_fromdata(&section, 0, psi, len) == PSI_RC_OK)
		pat_section_add(a, &section);
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

int dmx_init(struct adapter_s *adapter) {
	struct dmx_filter	df;
	int			dmxfd, i;

	/* FIXME Check for open success */
	dmxfd=open(dmxname(adapter->no), O_RDWR);

	if (dmxfd < 0) {
		logwrite(LOG_ERROR, "dmx: error opening dmx device %s",
				dmxname(adapter->no));
		return 0;
	}

	event_set(&adapter->dmxevent, dmxfd, EV_READ|EV_PERSIST, dmx_read, adapter);
	event_add(&adapter->dmxevent, NULL);

	memset(&df, 0, sizeof(struct dmx_filter));

	/* Prepare filter - give us section 0x0 aka PAT */
	df.filter[0]=0x0;
	df.mask[0]=0xff;

	/* Set filter and immediatly start receiving packets */
	demux_set_sct_filter(dmxfd, 0, &df, DMX_IMMEDIATE_START|DMX_CHECK_CRC, 0);

	/* Reset dmxfd fds - Run until 0x2000 as thats the budget mode pid */
	for(i=0;i<=PID_MAX+1;i++)
		adapter->dmxfd[i]=-1;

	return 1;
}
