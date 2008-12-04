
#include "getstream.h"
#include "output.h"

void stream_send(void *data, void *arg) {
	struct stream_s		*stream=arg;
	GList	*ol=g_list_first(stream->output);

	while(ol) {
		struct output_s	*o=ol->data;
		output_send(o, data);
		ol=g_list_next(ol);
	}
}

static void stream_init_pat(struct stream_s *stream);

static void stream_send_pat(int fd, short event, void *arg) {
	struct stream_s *stream=arg;
	struct pat_s	*pat;
	int		pkts;
	GList		*il;

	pat=pat_new();

	for(il=g_list_first(stream->input);il;il=g_list_next(il)) {
		struct input_s	*input=il->data;

		if (input->type == INPUT_PNR) {
			unsigned int	pmtpid=pmt_get_pmtpid(input->pnr.program);
			pat_add_program(pat, input->pnr.pnr, pmtpid);
		}
	}

	/* FIXME - we should take care on the PAT version and Transport ID */
	pkts=pat_send(pat, stream->patcc, 0, 0, stream_send, stream);

	pat_free(pat);

	stream->patcc=(stream->patcc+pkts)&TS_CC_MASK;

	stream_init_pat(stream);
}

static void stream_init_pat(struct stream_s *stream) {
	struct timeval	tv;

#define PAT_INTERVAL	500

	tv.tv_usec=PAT_INTERVAL*1000;
	tv.tv_sec=0;

	evtimer_set(&stream->patevent, stream_send_pat, stream);
	evtimer_add(&stream->patevent, &tv);
}

/*
 * Called initially on programm start to initialize all input
 * filter and outputs before the first TS packets get forwarded
 */
void stream_init(struct stream_s *stream) {
	GList		*il=g_list_first(stream->input);
	GList		*ol=g_list_first(stream->output);

	while(ol) {
		struct output_s	*output=ol->data;
		output_init(output);
		ol=g_list_next(ol);
	}

	/*
	 * FIXME - In case we dont have filters we might want to hand
	 * out the output_??? function onto the input functions to pass
	 * onto the demux or PMT parsing. This would eliminate the
	 * stream_send function and save CPU cycles.
	 */

	while(il) {
		struct input_s	*input=il->data;
		input_init(input);
		il=g_list_next(il);
	}

	/*
	 * If we not only have static content in our stream we might need to
	 * send out PATs on a regular basis - Initialize the PAT timer for this stream
	 */
	if (stream->psineeded)
		stream_init_pat(stream);

	/* FIXME SAP init ? */
	/* FIXME filter init ? */
}
