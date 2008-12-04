
#include "getstream.h"

static void input_init_pnr(struct input_s *input) {
	input->pnr.program=pmt_join_pnr(input->stream->adapter,
					input->pnr.pnr,
					stream_send,
					input->stream);
}

/*
 * Static PID input - Just add a callback to the dvr demux (e.g. adapter pidtable)
 * and direct it to the stream input callback
 *
 */
static void input_init_pid(struct input_s *input) {
	input->pid.cbkey=dvr_add_pcb(input->stream->adapter,
					input->pid.pid,
					DVRCB_TS,
					PID_STATIC,
					stream_send,
					input->stream);
}

void input_init(struct input_s	*input) {
	switch(input->type) {
		case(INPUT_PNR):
			input_init_pnr(input);
			break;
		case(INPUT_PID):
			input_init_pid(input);
			break;
		default:
			logwrite(LOG_ERROR, "input: Unknown input type %d", input->type);
			break;
	}
}
