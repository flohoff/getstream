/*
 *
 * Stream PIPE output - Stream a program to a FIFO for postprocessing
 *
 * One of the problems i could imaging is that for a program which does not get
 * and traffic e.g. TS Packets we'll never discover that the reader closed the pipe
 * as we never issue a write. This shouldnt be a problem but you are warned. Flo 2007-04-19
 *
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "output.h"
#include "simplebuffer.h"
#include "getstream.h"

#define PIPE_MAX_TS			(2048/TS_PACKET_SIZE)
#define PIPE_INTERVAL			5

static int output_pipe_tryopen(struct output_s *o) {
	o->pipe.fd=open(o->pipe.filename, O_NONBLOCK|O_WRONLY);

	if (o->pipe.fd >= 0) {
		logwrite(LOG_INFO, "stream_pipe: starting to write to %s - got reader", o->pipe.filename);
		o->receiver++;
		return 1;
	}

	if (errno == ENXIO)
		return 0;

	logwrite(LOG_ERROR, "stream_pipe: failed to open fifo %s", o->pipe.filename);

	return 0;
}

static void output_pipe_open_event(int fd, short event, void *arg);

static void output_pipe_event_init(struct output_s *o) {
	struct timeval	tv;

	tv.tv_usec=0;
	tv.tv_sec=PIPE_INTERVAL;

	evtimer_set(&o->pipe.event, output_pipe_open_event, o);
	evtimer_add(&o->pipe.event, &tv);
}

static void output_pipe_open_event(int fd, short event, void *arg) {
	struct output_s		*o=arg;

	/* Try opening - if it fails - retry */
	if (!output_pipe_tryopen(o))
		output_pipe_event_init(o);
}

static void output_pipe_close(struct output_s *o) {
	o->receiver--;
	close(o->pipe.fd);
	logwrite(LOG_INFO, "stream_pipe: closing %s - reader exited", o->pipe.filename);

	/* PIPE is closed - try opening on regular interval */
	output_pipe_event_init(o);
}

int output_init_pipe(struct output_s *o) {
	struct stat		st;

	if (!lstat(o->pipe.filename, &st)) {
		if (!S_ISFIFO(st.st_mode)) {
			logwrite(LOG_ERROR, "stream_pipe: %s exists and is not a pipe", o->pipe.filename);
			return 0;
		}
	} else {
		/* if lstat fails we possibly have no fifo */
		/* FIXME we need to check errno for non existant */
		if (mknod(o->pipe.filename, S_IFIFO|0700, 0)) {
			logwrite(LOG_ERROR, "stream_pipe: failed to create fifo %s", o->pipe.filename);
			return 0;
		}
	}

	o->buffer=sb_init(PIPE_MAX_TS, TS_PACKET_SIZE, 0);

	if (!o->buffer)
		return 0;

	signal(SIGPIPE, SIG_IGN);

	output_pipe_event_init(o);

	return 1;
}

void output_send_pipe(struct output_s *o, uint8_t *tsp) {
	int	len;

	sb_add_atoms(o->buffer, tsp, 1);

	if (!sb_free_atoms(o->buffer)) {
		len=write(o->pipe.fd, sb_bufptr(o->buffer), sb_buflen(o->buffer));

		/*
		 * We zap the buffer if we succeeded writing or not - there is no point
		 * in keeping the data - If the reader aint fast enough there is no point
		 * in buffering.
		 */
		sb_zap(o->buffer);

		if (len < 0) {
			if (errno == EPIPE)
				output_pipe_close(o);
			return;
		}

		/* FIXME - We might want to do more graceful here. We tried
		 * writing multiple TS packets to the FIFO and it failed. We
		 * now discard ALL packets in our buffer so we might loose some.
		 * A more graceful way would be to retry writing - For this we
		 * might need a different buffer design e.g. a ringbuffr
		 *
		 * Use libevent to get a callback when the reader is ready ?
		 *
		 */
	}

}

