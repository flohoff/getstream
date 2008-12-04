
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <event.h>

#include "getstream.h"
#include "config.h"
#include "libhttp.h"

#ifdef DEBUG
static int	guardcounter;

void *guard_thread(void *mtp) {
	pthread_t	mt=(pthread_t) mtp;
	struct timeval	tv;

	guardcounter=0;

	while(1) {
		tv.tv_sec=1;
		tv.tv_usec=0;
		select(0, NULL, NULL, NULL, &tv);

		if (guardcounter==0) {
			pthread_kill(mt, SIGABRT);
			pthread_exit(NULL);
		}
		guardcounter=0;
	}
}

static void init_guard_evtimer(void );

static void guard_evtimer(int fd, short event, void *arg) {
	guardcounter++;
	init_guard_evtimer();
}

static void init_guard_evtimer(void ) {
	static struct event	gevent;
	static struct timeval	tv;

	tv.tv_usec=50000;
	tv.tv_sec=00;

	evtimer_set(&gevent, guard_evtimer, &gevent);
	evtimer_add(&gevent, &tv);
}

void init_guard_thread(void ) {
	pthread_t	mt, gt;

	init_guard_evtimer();
	mt=pthread_self();
	pthread_create(&gt, NULL, guard_thread, (void *) mt);
}
#endif


/*
 * When profiling one needs to call "exit" or return to main.
 * As we are running in a wheel like a Hamster there is no way
 * returning to main so we unconditionally call exit after a timeout
 *
 */
static void terminate_timeout(int fd, short event, void *arg) {
	exit(0);
}

static void terminate_init(int timeout) {
	static struct event	event;
	static struct timeval	tv;

	tv.tv_usec=0;
	tv.tv_sec=timeout;

	evtimer_set(&event, terminate_timeout, &event);
	evtimer_add(&event, &tv);

}


static void usage(void ) {
	fprintf(stderr, "-c <config file> -d -t <timeout>\n");
	exit(-1);
}

struct http_server	*hserver;

int main(int argc, char **argv) {
	extern char		*optarg;
	char			ch;
	int			timeout=0;
	GList			*al;
	struct config_s		*config=NULL;

	while((ch=getopt(argc, argv, "c:dt:")) != -1) {
		switch(ch) {
			case 'c':
				config=readconfig(optarg);
				if (!config)
					exit(1);
				break;
			case 'd':
				logwrite_inc_level();
				break;
			case 't':
				timeout=strtol(optarg, NULL, 10);
				break;
			default:
				usage();
				break;
		}
	}
	if (!config)
		exit(-1);

	/* Initialize libevent */
	event_init();

	if (config->http_port) {
		hserver=http_init(config->http_port);
		if (!hserver) {
			fprintf(stderr, "Could not create http server on port %d\n", config->http_port);
			exit(-1);
		}
	}

	/* In case of profiling we want to call exit after a timeout */
	if (timeout)
		terminate_init(timeout);

	al=g_list_first(config->adapter);
	while(al) {
		struct adapter_s	*a=al->data;
		GList			*sl=g_list_first(a->streams);

		/* Tune to the one and only transponder */
		fe_tune_init(a);

		/* Initialize demux0 dvr0 and co */
		dmx_init(a);
		dvr_init(a);

		/* Init all streams */
		while(sl) {
			struct stream_s	*stream=sl->data;
			stream_init(stream);
			sl=g_list_next(sl);
		}

		al=g_list_next(al);
	}

#ifdef DEBUG
	init_guard_thread();
#endif

	/* Pigs can fly */
	event_dispatch();

	return 0;
}
