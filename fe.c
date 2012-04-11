#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include "getstream.h"

#define FE_CHECKSTATUS_INTERVAL	5 /* sec  */

struct diseqc_cmd {
	struct dvb_diseqc_master_cmd cmd;
	uint32_t wait;
};

struct diseqc_cmd switch_cmds[] = {
	{ { { 0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xf2, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xf1, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xf3, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xf4, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xf6, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xf5, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xf7, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xf8, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xfa, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xf9, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xfb, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xfc, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xfe, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xfd, 0x00, 0x00 }, 4 }, 0 },
	{ { { 0xe0, 0x10, 0x38, 0xff, 0x00, 0x00 }, 4 }, 0 }
};

static inline void msleep(uint32_t msec)
{
	struct timespec req = { msec / 1000, 1000000 * (msec % 1000) };

	while (nanosleep(&req, &req))
		;
}

int diseqc_send_msg(int fd, fe_sec_voltage_t v, struct diseqc_cmd **cmd, fe_sec_tone_mode_t t, fe_sec_mini_cmd_t b) {
	int err;

	if ((err = ioctl(fd, FE_SET_TONE, SEC_TONE_OFF)))
		return err;

	if ((err = ioctl(fd, FE_SET_VOLTAGE, v)))
		return err;

	msleep(15);
	while (*cmd) {
		if ((err = ioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &(*cmd)->cmd)))
			return err;

		msleep((*cmd)->wait);
		cmd++;
	}

	msleep(15);

	if ((err = ioctl(fd, FE_DISEQC_SEND_BURST, b)))
		return err;

	msleep(15);

	return ioctl(fd, FE_SET_TONE, t);
}


int setup_switch(int frontend_fd, int switch_pos, int voltage_18, int hiband) {
	struct diseqc_cmd *cmd[2] = { NULL, NULL };
	int i = 4 * switch_pos + 2 * hiband + (voltage_18 ? 1 : 0);

	if (i < 0 || i >= (int) (sizeof(switch_cmds)/sizeof(struct diseqc_cmd)))
		return -EINVAL;

	cmd[0] = &switch_cmds[i];

	return diseqc_send_msg (frontend_fd,
				i % 2 ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13,
				cmd,
				(i/2) % 2 ? SEC_TONE_ON : SEC_TONE_OFF,
				(i/4) % 2 ? SEC_MINI_B : SEC_MINI_A);
}


/*
 * Dump FrontEnd Status byte as clear text returned
 * by GET_STATUS or GET_EVENT ioctl
 *
 */
char *fe_decode_status(int status) {
	static char	str[256];

	str[0]=0x0;

	if (status & FE_HAS_SIGNAL)
		strcat(str, "HAS_SIGNAL ");
	if (status & FE_HAS_CARRIER)
		strcat(str, "HAS_CARRIER ");
	if (status & FE_HAS_VITERBI)
		strcat(str, "HAS_VITERBI ");
	if (status & FE_HAS_SYNC)
		strcat(str, "HAS_SYNC ");
	if (status & FE_HAS_LOCK)
		strcat(str, "HAS_LOCK ");
	if (status & FE_TIMEDOUT)
		strcat(str, "TIMEDOUT ");
	if (status & FE_REINIT)
		strcat(str, "REINIT ");

	/* Eliminate last space */
	if (strlen(str) > 0)
		str[strlen(str)-1]=0x0;

	return str;
};

static int fe_get_freqoffset(struct adapter_s *adapter) {
	int	freqoffset;

	if (adapter->fe.dvbs.t_freq <= 2200000)
		return adapter->fe.dvbs.t_freq;

	if (adapter->fe.dvbs.lnb_slof) {
		/* Ku Band LNB */
		if (adapter->fe.dvbs.t_freq < adapter->fe.dvbs.lnb_slof) {
			freqoffset=(adapter->fe.dvbs.t_freq-
					adapter->fe.dvbs.lnb_lof1);
		} else {
			freqoffset=(adapter->fe.dvbs.t_freq-
					adapter->fe.dvbs.lnb_lof2);
		}
	} else {
		/* C Band LNB */
		if (adapter->fe.dvbs.lnb_lof2) {
			if (adapter->fe.dvbs.t_pol == POL_H) {
				freqoffset=(adapter->fe.dvbs.lnb_lof2-
					adapter->fe.dvbs.t_freq);
			} else {
				freqoffset=(adapter->fe.dvbs.lnb_lof1-
					adapter->fe.dvbs.t_freq);
			}
		} else {
			freqoffset=adapter->fe.dvbs.lnb_lof1-adapter->fe.dvbs.t_freq;
		}
	}

	return freqoffset;
}

static int fe_get_voltage(struct adapter_s *adapter) {
	return (adapter->fe.dvbs.t_pol == POL_H) ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13;
}

static int fe_is_highband(struct adapter_s *adapter) {
	return (adapter->fe.dvbs.t_freq > adapter->fe.dvbs.lnb_slof);
}

static int fe_get_tone(struct adapter_s *adapter) {
	return (fe_is_highband(adapter) ? SEC_TONE_ON : SEC_TONE_OFF);
}

static int fe_tune_dvbs(struct adapter_s *adapter) {
	struct dvb_frontend_parameters	feparams;
	int				voltage, tone=SEC_TONE_OFF;

	memset(&feparams, 0, sizeof(struct dvb_frontend_parameters));

	voltage=fe_get_voltage(adapter);

	if (adapter->fe.dvbs.t_freq > 2200000) {
		if (adapter->fe.dvbs.lnb_slof) {
			/* Ku Band LNB */
			if (adapter->fe.dvbs.t_freq < adapter->fe.dvbs.lnb_slof) {
				feparams.frequency=(adapter->fe.dvbs.t_freq-
						adapter->fe.dvbs.lnb_lof1);
				tone = SEC_TONE_OFF;
			} else {
				feparams.frequency=(adapter->fe.dvbs.t_freq-
						adapter->fe.dvbs.lnb_lof2);
				tone = SEC_TONE_ON;
			}
		} else {
			/* C Band LNB */
			if (adapter->fe.dvbs.lnb_lof2) {
				if (adapter->fe.dvbs.t_pol == POL_H) {
					feparams.frequency=(adapter->fe.dvbs.lnb_lof2-
						adapter->fe.dvbs.t_freq);
				} else {
					feparams.frequency=(adapter->fe.dvbs.lnb_lof1-
						adapter->fe.dvbs.t_freq);
				}
			} else {
				feparams.frequency=adapter->fe.dvbs.lnb_lof1-adapter->fe.dvbs.t_freq;
			}
		}
	} else {
		feparams.frequency=adapter->fe.dvbs.t_freq;
	}

	feparams.inversion=INVERSION_AUTO;

	DVBS_SET_SYMBOLRATE(&feparams, adapter->fe.dvbs.t_srate);
	DVBS_SET_FEC(&feparams, FEC_AUTO);

	if (adapter->fe.dvbs.lnbsharing) {
		int value=SEC_TONE_OFF;
		logwrite(LOG_DEBUG, "fe: Adapter %d lnb-sharing active - trying to turn off", adapter->no);

		if (ioctl(adapter->fe.fd, FE_SET_TONE, value) < 0) {
			logwrite(LOG_ERROR, "fe: Adapter %d ioctl FE_SET_TONE failed - %s", adapter->no, strerror(errno));
		}

		value=SEC_VOLTAGE_OFF;
		if (ioctl(adapter->fe.fd, FE_SET_VOLTAGE, value) < 0) {
			if (errno == EINVAL) {
				logwrite(LOG_DEBUG, "fe: Adapter %d SEC_VOLTAGE_OFF not possible", adapter->no);
				if (ioctl(adapter->fe.fd, FE_SET_VOLTAGE, voltage) < 0) {
					logwrite(LOG_ERROR, "fe: Adapter %d ioctl FE_SET_VOLTAGE failed - %s", adapter->no, strerror(errno));
				}
			}
		}
	} else if (adapter->fe.dvbs.t_diseqc) {
		if (setup_switch(adapter->fe.fd, adapter->fe.dvbs.t_diseqc-1, voltage, (tone == SEC_TONE_ON))) {
			logwrite(LOG_ERROR, "fe: diseqc failed to send");
			exit(-1);
		}
		logwrite(LOG_DEBUG, "fe: diseqc send successful");
		sleep(1);
	} else {
		if (ioctl(adapter->fe.fd, FE_SET_VOLTAGE, voltage) < 0) {
			logwrite(LOG_ERROR, "fe: ioctl FE_SET_VOLTAGE failed - %s", strerror(errno));
		}

		if (ioctl(adapter->fe.fd, FE_SET_TONE, tone) < 0) {
			logwrite(LOG_ERROR, "fe: ioctl FE_SET_TONE failed - %s", strerror(errno));
		}
	}

	logwrite(LOG_INFO, "fe: DVB-S tone = %d", tone);
	logwrite(LOG_INFO, "fe: DVB-S voltage = %d", voltage);
	logwrite(LOG_INFO, "fe: DVB-S diseqc = %d", adapter->fe.dvbs.t_diseqc);
	logwrite(LOG_INFO, "fe: DVB-S freq = %lu", adapter->fe.dvbs.t_freq);
	logwrite(LOG_INFO, "fe: DVB-S lof1 = %lu", adapter->fe.dvbs.lnb_lof1);
	logwrite(LOG_INFO, "fe: DVB-S lof2 = %lu", adapter->fe.dvbs.lnb_lof2);
	logwrite(LOG_INFO, "fe: DVB-S slof = %lu", adapter->fe.dvbs.lnb_slof);
	logwrite(LOG_INFO, "fe: DVB-S feparams.frequency = %d", feparams.frequency);
	logwrite(LOG_INFO, "fe: DVB-S feparams.inversion = %d", feparams.inversion);
	logwrite(LOG_INFO, "fe: DVB-S feparams.u.qpsk.symbol_rate = %d", adapter->fe.dvbs.t_srate);

	if (ioctl(adapter->fe.fd, FE_SET_FRONTEND, &feparams) < 0) {
		logwrite(LOG_ERROR, "fe: ioctl FE_SET_FRONTEND failed - %s", strerror(errno));
		exit(-1);
	}

	return 0;
}

int fe_tune_dvbt(struct adapter_s *adapter) {
	struct dvb_frontend_parameters		feparams;

	memset(&feparams, 0, sizeof(struct dvb_frontend_parameters));

	feparams.frequency = adapter->fe.dvbt.freq;
	feparams.inversion = INVERSION_AUTO;

	DVBT_SET_CODERATE_HP(&feparams, FEC_AUTO);
	DVBT_SET_CODERATE_LP(&feparams, FEC_AUTO);

	switch(adapter->fe.dvbt.bandwidth) {
		case(0): DVBT_SET_BANDWIDTH(&feparams, BANDWIDTH_AUTO); break;
		case(6): DVBT_SET_BANDWIDTH(&feparams, BANDWIDTH_6_MHZ); break;
		case(7): DVBT_SET_BANDWIDTH(&feparams, BANDWIDTH_7_MHZ); break;
		case(8): DVBT_SET_BANDWIDTH(&feparams, BANDWIDTH_8_MHZ); break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T bandwidth %d", adapter->fe.dvbt.bandwidth);
			exit(-1);
	}

	switch(adapter->fe.dvbt.modulation) {
		case(0): DVBT_SET_MODULATION(&feparams, QAM_AUTO); break;
		case(16):DVBT_SET_MODULATION(&feparams, QAM_16); break;
		case(32):DVBT_SET_MODULATION(&feparams, QAM_32); break;
		case(64):DVBT_SET_MODULATION(&feparams, QAM_64); break;
		case(128):DVBT_SET_MODULATION(&feparams, QAM_128); break;
		case(256):DVBT_SET_MODULATION(&feparams, QAM_256); break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T modulation %d", adapter->fe.dvbt.modulation);
			exit(-1);
	}

	switch(adapter->fe.dvbt.tmode) {
		case(0):DVBT_SET_TMODE(&feparams, TRANSMISSION_MODE_AUTO); break;
		case(2):DVBT_SET_TMODE(&feparams, TRANSMISSION_MODE_2K); break;
		case(8):DVBT_SET_TMODE(&feparams, TRANSMISSION_MODE_8K); break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T transmission mode %d", adapter->fe.dvbt.tmode);
			exit(-1);
	}

	switch(adapter->fe.dvbt.guard) {
		case(0):DVBT_SET_GUARD(&feparams, GUARD_INTERVAL_AUTO); break;
		case(4):DVBT_SET_GUARD(&feparams, GUARD_INTERVAL_1_4); break;
		case(8):DVBT_SET_GUARD(&feparams, GUARD_INTERVAL_1_8); break;
		case(16):DVBT_SET_GUARD(&feparams, GUARD_INTERVAL_1_16); break;
		case(32):DVBT_SET_GUARD(&feparams, GUARD_INTERVAL_1_32); break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T guard interval %d", adapter->fe.dvbt.guard);
			exit(-1);
	}

	switch(adapter->fe.dvbt.hierarchy) {
		case(-1):DVBT_SET_HIERARCHY(&feparams, HIERARCHY_NONE); break;
		case(0):DVBT_SET_HIERARCHY(&feparams, HIERARCHY_AUTO); break;
		case(1):DVBT_SET_HIERARCHY(&feparams, HIERARCHY_1); break;
		case(2):DVBT_SET_HIERARCHY(&feparams, HIERARCHY_2); break;
		case(4):DVBT_SET_HIERARCHY(&feparams, HIERARCHY_4); break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T hierarchy %d", adapter->fe.dvbt.hierarchy);
			exit(-1);
	}

	if (ioctl(adapter->fe.fd, FE_SET_FRONTEND, &feparams) < 0) {
		logwrite(LOG_ERROR, "ioctl FE_SET_FRONTEND failed");
		exit(-1);
	}

	return 0;
}

#if (DVB_API_VERSION>=5)

static int fe_tune_dvbs2(struct adapter_s *adapter) {
	struct dtv_property	p[DTV_IOCTL_MAX_MSGS];
	struct dtv_properties	cmds;

	p[0].cmd = DTV_CLEAR;
	p[1].cmd = DTV_DELIVERY_SYSTEM; p[1].u.data = SYS_DVBS2;
	p[2].cmd = DTV_SYMBOL_RATE;	p[2].u.data = adapter->fe.dvbs.t_srate;
	p[3].cmd = DTV_INNER_FEC;	p[3].u.data = FEC_AUTO;
	p[4].cmd = DTV_INVERSION;	p[4].u.data = INVERSION_AUTO;
	p[5].cmd = DTV_FREQUENCY;	p[5].u.data = fe_get_freqoffset(adapter);
	p[6].cmd = DTV_VOLTAGE;		p[6].u.data = fe_get_voltage(adapter);
	p[7].cmd = DTV_TONE;		p[7].u.data = fe_get_tone(adapter);
	p[8].cmd = DTV_TUNE;		p[8].u.data = 0;

	cmds.num=9;
	cmds.props=p;

	if (ioctl(adapter->fe.fd, FE_SET_PROPERTY, &cmds) < 0) {
		logwrite(LOG_ERROR, "fe: ioctl FE_SET_PROPERTY failed - %s", strerror(errno));
		exit(-1);
	}

	return 0;
}
#else
static int fe_tune_dvbs2(struct adapter_s *adapter) {
	logwrite(LOG_ERROR, "fe: not compiled against DVB Api 5 - no DVB-S2 support");
	exit(-1);
}
#endif

static int fe_tune_dvbc(struct adapter_s *adapter) {
	struct dvb_frontend_parameters	feparams;

	memset(&feparams, 0, sizeof(struct dvb_frontend_parameters));

	feparams.frequency = adapter->fe.dvbc.freq;

	DVBC_SET_SYMBOLRATE(&feparams, adapter->fe.dvbc.srate);

	feparams.inversion=INVERSION_AUTO;

	switch(adapter->fe.dvbc.modulation) {
		case -1: DVBC_SET_MODULATION(&feparams, QPSK); break;
		case 0: DVBC_SET_MODULATION(&feparams, QAM_AUTO); break;
		case 16: DVBC_SET_MODULATION(&feparams, QAM_16); break;
		case 32: DVBC_SET_MODULATION(&feparams, QAM_32); break;
		case 64: DVBC_SET_MODULATION(&feparams, QAM_64); break;
		case 128: DVBC_SET_MODULATION(&feparams, QAM_128); break;
		case 256: DVBC_SET_MODULATION(&feparams, QAM_256); break;
		default:
			logwrite(LOG_ERROR, "Unknown modulation %d", adapter->fe.dvbc.modulation);
			exit(-1);
	}
	switch(adapter->fe.dvbc.fec) {
		case 0: DVBC_SET_FEC(&feparams, FEC_NONE); break;
		case 1: DVBC_SET_FEC(&feparams, FEC_1_2); break;
		case 2: DVBC_SET_FEC(&feparams, FEC_2_3); break;
		case 3: DVBC_SET_FEC(&feparams, FEC_3_4); break;
		case 4: DVBC_SET_FEC(&feparams, FEC_4_5); break;
		case 5: DVBC_SET_FEC(&feparams, FEC_5_6); break;
		case 6: DVBC_SET_FEC(&feparams, FEC_6_7); break;
		case 7: DVBC_SET_FEC(&feparams, FEC_7_8); break;
		case 8: DVBC_SET_FEC(&feparams, FEC_8_9); break;
		case 9: DVBC_SET_FEC(&feparams, FEC_AUTO); break;
		default:
			logwrite(LOG_ERROR, "Unknown fec %d", adapter->fe.dvbc.fec);
			exit(-1);
	}

	if (ioctl(adapter->fe.fd, FE_SET_FRONTEND, &feparams) < 0) {
		logwrite(LOG_ERROR, "ioctl FE_SET_FRONTEND failed");
		exit(-1);
	}

	return 0;
}

static int fe_tune(struct adapter_s *adapter) {
	switch(adapter->type) {
		case(AT_DVBS2):
			fe_tune_dvbs2(adapter);
			break;
		case(AT_DVBS):
			fe_tune_dvbs(adapter);
			break;
		case(AT_DVBT):
			fe_tune_dvbt(adapter);
			break;
		case(AT_DVBC):
			fe_tune_dvbc(adapter);
			break;
	}

	adapter->fe.tunelast=time(NULL);

	return 0;
}


#define FE_TUNE_MINDELAY	5
void fe_retune(struct adapter_s *adapter) {
	time_t		now;

	now=time(NULL);

	/* Debounce the retuning */
	if (adapter->fe.tunelast + FE_TUNE_MINDELAY > now)
		return;

	fe_tune(adapter);
}

#if (DVB_API_VERSION>=5)
static int fe_api5_checkcap(struct adapter_s *adapter) {
	struct dtv_property p[1];
	struct dtv_properties cmds;

	p[0].cmd = DTV_DELIVERY_SYSTEM;

	cmds.props = p;
	cmds.num = 1;

	if (ioctl(adapter->fe.fd, FE_GET_PROPERTY, &cmds)) {
		logwrite(LOG_DEBUG, "fe: ioctl(FE_GET_PROPERTY) failed - no DVBS2 aka API 5 support?");
		return 0;
	}

	switch (p[0].u.data) {
		case(SYS_DVBS):
			if (adapter->type == AT_DVBS)
				break;
			logwrite(LOG_ERROR, "fe: Adapter %d is an DVB-S card - config is not for DVB-S", adapter->no);
			exit(-1);
		case(SYS_DVBS2):
			if (adapter->type == AT_DVBS || adapter->type == AT_DVBS2)
				break;
			logwrite(LOG_ERROR, "fe: Adapter %d is an DVB-S2 card - config is not DVB-S or S2", adapter->no);
			exit(-1);
		case(SYS_DVBT):
			if (adapter->type == AT_DVBT)
				break;
			logwrite(LOG_ERROR, "fe: Adapter %d is an DVB-T card - config is not for DVB-T", adapter->no);
			exit(-1);
		case(SYS_DVBC_ANNEX_B):
		case(SYS_DVBC_ANNEX_AC):
			if (adapter->type == AT_DVBC)
				break;
			logwrite(LOG_ERROR, "fe: Adapter %d is an DVB-C card - config is not for DVB-C", adapter->no);
			exit(-1);
	}

	return 1;
}
#else
static int fe_api5_checkcap(struct adapter_s *adapter) {
	return 0;
}
#endif

static void fe_api3_checkcap(struct adapter_s *adapter) {
	char		*type="unknown";

	if (ioctl(adapter->fe.fd, FE_GET_INFO, &adapter->fe.feinfo)) {
		logwrite(LOG_ERROR, "fe: ioctl(FE_GET_INFO...) failed");
		exit(-1);
	}

	switch(adapter->fe.feinfo.type) {
		case(FE_QPSK):
			type="QPSK";
			if (adapter->type == AT_DVBS)
				break;
			logwrite(LOG_ERROR, "fe: Adapter %d is an DVB-S card - config is not for DVB-S", adapter->no);
			break;
			//exit(-1);
		case(FE_OFDM):
			type="OFDM";
			if (adapter->type == AT_DVBT)
				break;
			logwrite(LOG_ERROR, "fe: Adapter %d is an DVB-T card - config is not for DVB-T", adapter->no);
			exit(-1);
		case(FE_QAM):
			type="QAM";
			if (adapter->type == AT_DVBC)
				break;
			logwrite(LOG_ERROR, "fe: Adapter %d is an DVB-C card - config is not for DVB-C", adapter->no);
			exit(-1);
		default:
			logwrite(LOG_ERROR, "fe: Adapter %d is an unknown card type %d", adapter->no, adapter->fe.feinfo.type);
			break;
	}

	if (adapter->fe.feinfo.type == FE_QPSK && !(adapter->fe.feinfo.caps & FE_CAN_FEC_AUTO)) {
		logwrite(LOG_ERROR, "fe: adapter %d is incapable of handling FEC_AUTO - please report to flo@rfc822.org", adapter->no);
	}

	logwrite(LOG_DEBUG, "fe: adapter %d type %s name \"%s\"",
				adapter->no,
				type,
				adapter->fe.feinfo.name);

	logwrite(LOG_DEBUG, "fe: adapter %d frequency min %d max %d step %d tolerance %d",
				adapter->no,
				adapter->fe.feinfo.frequency_min,
				adapter->fe.feinfo.frequency_max,
				adapter->fe.feinfo.frequency_stepsize,
				adapter->fe.feinfo.frequency_tolerance);

	logwrite(LOG_DEBUG, "fe: adapter %d symbol rate min %d max %d tolerance %d",
				adapter->no,
				adapter->fe.feinfo.symbol_rate_min,
				adapter->fe.feinfo.symbol_rate_max,
				adapter->fe.feinfo.symbol_rate_tolerance);
}

static void fe_checkcap(struct adapter_s *adapter) {

	if (fe_api5_checkcap(adapter))
		return;

	fe_api3_checkcap(adapter);

	return;
}


static void *fe_event_checkstatus_thread(void *_data) {
	struct adapter_s *adapter=_data;
	fd_set except_set;
	int ret;
	int res;
	int status;
	struct dvb_frontend_event event;
	struct timeval tv;

	while (42) {
		FD_ZERO(&except_set);
		FD_SET(adapter->fe.fd, &except_set);
		tv.tv_sec = FE_CHECKSTATUS_INTERVAL;
		tv.tv_usec = 0;

		ret = select(adapter->fe.fd, NULL, NULL, &except_set, &tv);
		if (ret > 0) {
			/* Events arrived, check them.  */
			res = ioctl(adapter->fe.fd, FE_GET_EVENT, &event);
			if (res < 0 && errno != EOVERFLOW) {
				logwrite(LOG_ERROR, "fe: Adapter %d Status event %d = %s",
					 adapter->no, errno, strerror(errno));
				continue;
			}

			status = event.status;
			if (res >= 0 && status) {
				if (!(status & FE_TIMEDOUT)) {
					logwrite(LOG_INFO, "fe: Adapter %d Status: 0x%02x (%s)",
						 adapter->no, status, fe_decode_status(status));

					if (!(status & FE_HAS_LOCK))
						fe_retune(adapter);
				}
			}
		} else if (ret == 0) {
			res = ioctl(adapter->fe.fd, FE_READ_STATUS, &status);
			if (res == 0 && !(status & FE_HAS_LOCK)) {
				logwrite(LOG_INFO, "fe: Adapter %d Status: 0x%02x (%s)",
					 adapter->no, status, fe_decode_status(status));
				fe_retune(adapter);
			}
		}
	}

	return NULL;
}

int fe_tune_init(struct adapter_s *adapter) {
	char		fename[128];
	pthread_t	thread;

	sprintf(fename, "/dev/dvb/adapter%d/frontend0", adapter->no);

	adapter->fe.fd=open(fename, O_RDWR|O_NONBLOCK);

	if (adapter->fe.fd < 0) {
		logwrite(LOG_ERROR, "Error opening dvb frontend %s", fename);
		exit(-1);
	}

	logwrite(LOG_INFO, "fe: Adapter %d Setting up frontend tuner", adapter->no);

	fe_checkcap(adapter);

	/* Single shot - try to tune */
	fe_tune(adapter);

	/* Watch the filedescriptor for frontend events */
	pthread_create(&thread, NULL, &fe_event_checkstatus_thread, adapter);
	pthread_detach(thread);

	return 0;
}
