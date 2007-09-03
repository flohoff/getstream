#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <time.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include "getstream.h"

struct diseqc_cmd {
	struct dvb_diseqc_master_cmd cmd;
	uint32_t wait;
};

void diseqc_send_msg(int fd, fe_sec_voltage_t v, struct diseqc_cmd *cmd,
		fe_sec_tone_mode_t t, fe_sec_mini_cmd_t b) {

	ioctl(fd, FE_SET_TONE, SEC_TONE_OFF);
	ioctl(fd, FE_SET_VOLTAGE, v);
	usleep(15 * 1000);

	ioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &cmd->cmd);
	usleep(cmd->wait * 1000);
	usleep(15 * 1000);

	ioctl(fd, FE_DISEQC_SEND_BURST, b);
	usleep(15 * 1000);

	ioctl(fd, FE_SET_TONE, t);
}

/*
 * DIgital Satellite Equipment Control,
 * Specification is available from http://www.eutelsat.com/
 */
static int head_diseqc(int secfd, int satno, int voltage, int tone) {
	struct diseqc_cmd cmd = { {{0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4}, 0 };

	/*
	 * param: high nibble: reset bits, low nibble set bits,
	 * bits are: option, position, polarizaion, band
	 */
	cmd.cmd.msg[3] = 0xf0 |
		(((satno * 4) & 0x0f) |
		(voltage == SEC_VOLTAGE_13 ? 1 : 0) |
		(tone == SEC_TONE_ON ? 0 : 2));

	diseqc_send_msg(secfd,
			voltage,
			&cmd,
			tone,
			(satno / 4) % 2 ? SEC_MINI_B : SEC_MINI_A);

	return 1;
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
}

static int fe_tune_dvbs(struct adapter_s *adapter) {
	struct dvb_frontend_parameters	feparams;
	int				voltage, tone, i;

	memset(&feparams, 0, sizeof(struct dvb_frontend_parameters));

	if (strcasecmp(adapter->dvbs.t_pol, "h") == 0) {
		voltage=SEC_VOLTAGE_18;
	} else if(strcasecmp(adapter->dvbs.t_pol, "v") == 0) {
		voltage=SEC_VOLTAGE_13;
	} else {
		logwrite(LOG_ERROR, "fe: Unknown polarity \"%s\" in dvb-s transponder config", adapter->dvbs.t_pol);
		exit(-1);
	}

	if (adapter->dvbs.t_freq > 2200000) {
		if (adapter->dvbs.t_freq < adapter->dvbs.lnb_slof) {
			feparams.frequency=(adapter->dvbs.t_freq-adapter->dvbs.lnb_lof1);
			tone = SEC_TONE_OFF;
		} else {
			feparams.frequency=(adapter->dvbs.t_freq-adapter->dvbs.lnb_lof2);
			tone = SEC_TONE_ON;
		}
	} else {
		feparams.frequency=adapter->dvbs.t_freq;
	}

	feparams.inversion=INVERSION_AUTO;
	feparams.u.qpsk.symbol_rate=adapter->dvbs.t_srate;
	feparams.u.qpsk.fec_inner=FEC_AUTO;

	if (adapter->dvbs.t_diseqc) {
		for(i=0;i<=1;i++) {
			logwrite(LOG_DEBUG, "fe: diseqc sending command %d", i);

			if (!head_diseqc(adapter->fefd, adapter->dvbs.t_diseqc-1, voltage, tone)) {
				logwrite(LOG_ERROR, "fe: diseqc failed to send");
				exit(-1);
			}

			logwrite(LOG_DEBUG, "fe: diseqc send successful");
			sleep(1);
		}
	} else {
		if (ioctl(adapter->fefd, FE_SET_VOLTAGE, voltage) < 0) {
			logwrite(LOG_ERROR, "fe: ioctl FE_SET_VOLTAGE failed");
		}

		if (ioctl(adapter->fefd, FE_SET_TONE, tone) < 0) {
			logwrite(LOG_ERROR, "fe: ioctl FE_SET_VOLTAGE failed");
		}
	}

	logwrite(LOG_INFO, "fe: DVB-S tone = %d", tone);
	logwrite(LOG_INFO, "fe: DVB-S diseqc = %d", adapter->dvbs.t_diseqc);
	logwrite(LOG_INFO, "fe: DVB-S freq = %lu", adapter->dvbs.t_freq);
	logwrite(LOG_INFO, "fe: DVB-S lof1 = %lu", adapter->dvbs.lnb_lof1);
	logwrite(LOG_INFO, "fe: DVB-S lof2 = %lu", adapter->dvbs.lnb_lof2);
	logwrite(LOG_INFO, "fe: DVB-S slof = %lu", adapter->dvbs.lnb_slof);
	logwrite(LOG_INFO, "fe: DVB-S feparams.frequency = %d", feparams.frequency);
	logwrite(LOG_INFO, "fe: DVB-S feparams.inversion = %d", feparams.inversion);
	logwrite(LOG_INFO, "fe: DVB-S feparams.u.qpsk.symbol_rate = %d", feparams.u.qpsk.symbol_rate);
	logwrite(LOG_INFO, "fe: DVB-S feparams.u.qpsk.fec_inner = %d", feparams.u.qpsk.fec_inner);

	if (ioctl(adapter->fefd, FE_SET_FRONTEND, &feparams) < 0) {
		logwrite(LOG_ERROR, "fe: ioctl FE_SET_FRONTEND failed");
		exit(-1);
	}

	return 0;
}

int fe_tune_dvbt(struct adapter_s *adapter) {
	struct dvb_frontend_parameters	feparams;

	memset(&feparams, 0, sizeof(struct dvb_frontend_parameters));

	feparams.inversion = INVERSION_AUTO;
	feparams.frequency = adapter->dvbt.freq;

	switch(adapter->dvbt.bandwidth) {
		case(0):
			feparams.u.ofdm.bandwidth=BANDWIDTH_AUTO;
			break;
		case(6):
			feparams.u.ofdm.bandwidth=BANDWIDTH_6_MHZ;
			break;
		case(7):
			feparams.u.ofdm.bandwidth=BANDWIDTH_7_MHZ;
			break;
		case(8):
			feparams.u.ofdm.bandwidth=BANDWIDTH_8_MHZ;
			break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T bandwidth %d", adapter->dvbt.bandwidth);
			exit(-1);
	}

	feparams.u.ofdm.code_rate_HP = FEC_AUTO;
	feparams.u.ofdm.code_rate_LP = FEC_AUTO;

	switch(adapter->dvbt.modulation) {
		case(0):
			feparams.u.ofdm.constellation = QAM_AUTO;
			break;
		case(16):
			feparams.u.ofdm.constellation = QAM_16;
			break;
		case(32):
			feparams.u.ofdm.constellation = QAM_32;
			break;
		case(64):
			feparams.u.ofdm.constellation = QAM_64;
			break;
		case(128):
			feparams.u.ofdm.constellation = QAM_128;
			break;
		case(256):
			feparams.u.ofdm.constellation = QAM_256;
			break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T modulation %d", adapter->dvbt.modulation);
			exit(-1);
	}

	switch(adapter->dvbt.tmode) {
		case(0):
			feparams.u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;
			break;
		case(2):
			feparams.u.ofdm.transmission_mode = TRANSMISSION_MODE_2K;
			break;
		case(8):
			feparams.u.ofdm.transmission_mode = TRANSMISSION_MODE_8K;
			break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T transmission mode %d", adapter->dvbt.tmode);
			exit(-1);
	}

	switch(adapter->dvbt.guard) {
		case(0):
			feparams.u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;
			break;
		case(4):
			feparams.u.ofdm.guard_interval = GUARD_INTERVAL_1_4;
			break;
		case(8):
			feparams.u.ofdm.guard_interval = GUARD_INTERVAL_1_8;
			break;
		case(16):
			feparams.u.ofdm.guard_interval = GUARD_INTERVAL_1_16;
			break;
		case(32):
			feparams.u.ofdm.guard_interval = GUARD_INTERVAL_1_32;
			break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T guard interval %d", adapter->dvbt.guard);
			exit(-1);
	}


	switch(adapter->dvbt.hierarchy) {
		case(-1):
			feparams.u.ofdm.hierarchy_information = HIERARCHY_NONE;
			break;
		case(0):
			feparams.u.ofdm.hierarchy_information = HIERARCHY_AUTO;
			break;
		case(1):
			feparams.u.ofdm.hierarchy_information = HIERARCHY_1;
			break;
		case(2):
			feparams.u.ofdm.hierarchy_information = HIERARCHY_2;
			break;
		case(4):
			feparams.u.ofdm.hierarchy_information = HIERARCHY_4;
			break;
		default:
			logwrite(LOG_ERROR, "fe: Unknown DVB-T hierarchy %d", adapter->dvbt.hierarchy);
			exit(-1);
	}

	if (ioctl(adapter->fefd, FE_SET_FRONTEND, &feparams) < 0) {
		logwrite(LOG_ERROR, "ioctl FE_SET_FRONTEND failed");
		exit(-1);
	}

	return 0;
}

static int fe_tune_dvbc(struct adapter_s *adapter) {
	struct dvb_frontend_parameters	feparams;

	memset(&feparams, 0, sizeof(struct dvb_frontend_parameters));

	feparams.frequency = adapter->dvbc.freq;
	feparams.inversion=INVERSION_AUTO;
	feparams.u.qam.symbol_rate = adapter->dvbc.srate;
	switch(adapter->dvbc.modulation) {
		case -1: feparams.u.qam.modulation = QPSK; break;
		case 0: feparams.u.qam.modulation = QAM_AUTO; break;
		case 16: feparams.u.qam.modulation = QAM_16; break;
		case 32: feparams.u.qam.modulation = QAM_32; break;
		case 64: feparams.u.qam.modulation = QAM_64; break;
		case 128: feparams.u.qam.modulation = QAM_128; break;
		case 256: feparams.u.qam.modulation = QAM_256; break;
		default:
			logwrite(LOG_ERROR, "Unknown modulation %d", adapter->dvbc.modulation);
			exit(-1);
	}
	switch(adapter->dvbc.fec) {
		case 0: feparams.u.qam.fec_inner = FEC_NONE; break;
		case 1: feparams.u.qam.fec_inner = FEC_1_2; break;
		case 2: feparams.u.qam.fec_inner = FEC_2_3; break;
		case 3: feparams.u.qam.fec_inner = FEC_3_4; break;
		case 4: feparams.u.qam.fec_inner = FEC_4_5; break;
		case 5: feparams.u.qam.fec_inner = FEC_5_6; break;
		case 6: feparams.u.qam.fec_inner = FEC_6_7; break;
		case 7: feparams.u.qam.fec_inner = FEC_7_8; break;
		case 8: feparams.u.qam.fec_inner = FEC_8_9; break;
		case 9: feparams.u.qam.fec_inner = FEC_AUTO; break;
		default:
			logwrite(LOG_ERROR, "Unknown fec %d", adapter->dvbc.fec);
			exit(-1);
	}

	if (ioctl(adapter->fefd, FE_SET_FRONTEND, &feparams) < 0) {
		logwrite(LOG_ERROR, "ioctl FE_SET_FRONTEND failed");
		exit(-1);
	}

	return 0;
}

#if 0
static void fe_monitor(struct adapter_s *a) {
	uint32_t		ber;
	uint16_t		signal;
	uint16_t		snr;
	uint32_t		uncorrected;
	if (ioctl(a->fefd, FE_READ_BER, &ber) >= 0) {
		a->ber=ber;
	}
	if (ioctl(a->fefd, FE_READ_SNR, &snr) >= 0) {
		a->snr=snr;
	}
	if (ioctl(a->fefd, FE_READ_SIGNAL_STRENGTH, &signal) >= 0) {
		a->signal=signal;
	}

	ioctl(a->fefd, FE_READ_UNCORRECTED_BLOCKS, &uncorrected);

	logwrite(LOG_DEBUG, "fe: BER %d SNR %d Signal strength %d Uncorrected blocks %u",
		ber, snr, signal, uncorrected);
}
#endif

static int fe_tune(struct adapter_s *adapter) {
	logwrite(LOG_INFO, "fe: Setting up frontend tuner");

	switch(adapter->type) {
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

	adapter->fetunelast=time(NULL);

	return 0;
}


#define FE_TUNE_MINDELAY	5

void fe_retune(struct adapter_s *adapter) {
	time_t		now;

	now=time(NULL);

	/* Debounce the retuning */
	if (adapter->fetunelast + FE_TUNE_MINDELAY > now)
		return;

	fe_tune(adapter);
}

static void fe_timer_init(struct adapter_s *adapter);

static void fe_check_status(int fd, short event, void *arg) {
	struct adapter_s	*adapter=arg;
	fe_status_t		status;
	int			res;

	res=ioctl(adapter->fefd, FE_READ_STATUS, &status);

	if (res == 0) {
		if (!(status & FE_HAS_LOCK)) {
			logwrite(LOG_INFO, "fe: Adapter %d Status: 0x%02x (%s)",
					adapter->no, status, fe_decode_status(status));
			fe_retune(adapter);
		}
	}

	fe_timer_init(adapter);
}

#define FE_CHECKSTATUS_INTERVAL	5

static void fe_timer_init(struct adapter_s *adapter) {
	struct timeval	tv;

	tv.tv_sec=FE_CHECKSTATUS_INTERVAL;
	tv.tv_usec=0;

	evtimer_set(&adapter->fetimer, fe_check_status, adapter);
	evtimer_add(&adapter->fetimer, &tv);
}

/*
 * We had an event on the frontend filedescriptor - poll the event
 * and dump the status
 *
 */
static void fe_event(int fd, short ev, void *arg) {
	struct adapter_s		*adapter=arg;
	int				res, status;
	struct dvb_frontend_event	event;

	res=ioctl(adapter->fefd, FE_GET_EVENT, &event);

	if (res < 0 && errno != EOVERFLOW) {
		logwrite(LOG_ERROR, "fe: Adapter %d Status event overflow %d",
				adapter->no, errno);
		return;
	}

	if (res >= 0 && event.status) {
		if (!(event.status & FE_TIMEDOUT)) {
			status=event.status;

			logwrite(LOG_INFO, "fe: Adapter %d Status: 0x%02x (%s)",
				adapter->no, status, fe_decode_status(status));

			if (!(event.status & FE_HAS_LOCK)) {
				fe_retune(adapter);
			}
		}
	}
}

int fe_tune_init(struct adapter_s *adapter) {
	char		fename[128];

	sprintf(fename, "/dev/dvb/adapter%d/frontend0", adapter->no);

	adapter->fefd=open(fename, O_RDWR|O_NONBLOCK);

	if (!adapter->fefd) {
		logwrite(LOG_ERROR, "Error opening dvb frontend %s", fename);
		exit(-1);
	}

	/* Single shot - try to tune */
	fe_tune(adapter);

	/* Watch the filedescriptor for frontend events */
	event_set(&adapter->feevent, adapter->fefd, EV_READ|EV_PERSIST, fe_event, adapter);
	event_add(&adapter->feevent, NULL);

	/* Create a timer to regular poll the status */
	fe_timer_init(adapter);

	return 0;
}
