
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <assert.h>

/*
 * Ringbuffer implementation
 *
 *
 * head==tail		Ringbuffer empty
 * head==tail-1		Ringbuffer full
 *
 * H                 H
 * 1234567 -> 1234567
 * T          T
 *
 *
 *
 *
 *
 *
 */

struct ringbuffer_s {
	uint8_t		*buffer;

	unsigned int	atoms;
	unsigned int	atomsize;

	unsigned int	used;
	unsigned int	head;
	unsigned int	tail;
};

struct ringbuffer_s *ringbuffer_init(unsigned int atoms, unsigned int atomsize) {
	struct ringbuffer_s *rb;

	rb=calloc(1, sizeof(struct ringbuffer_s));
	if (!rb)
		return NULL;

	rb->atoms=atoms;
	rb->atomsize=atomsize;
	rb->buffer=malloc(atomsize*atoms);

	if (!rb->buffer) {
		free(rb);
		return NULL;
	}

	return rb;
}

static int ringbuffer_headroom(struct ringbuffer_s *rb) {

	/* If head == tail we are either full or empty */
	if (rb->used >= rb->atoms)
		return 0;

	if (rb->tail > rb->head)
		return rb->tail-rb->head;

	return rb->atoms-rb->head;
}

static void ringbuffer_push(struct ringbuffer_s *rb, int num) {
	rb->head+=num;

	if (rb->head >= rb->atoms)
		rb->head-=rb->atoms;

	rb->used+=num;

	assert(rb->used <= rb->atoms);
}

static int ringbuffer_add_atoms(struct ringbuffer_s *rb, uint8_t *atom, int num) {
	int		headroom, atoms;
	uint8_t		*dptr;

	headroom=ringbuffer_headroom(rb);
	if (!headroom)
		return 0;

	atoms=MIN(headroom, num);
	dptr=&rb->buffer[rb->head*rb->atomsize];
	memcpy(dptr, atom, atoms*rb->atomsize);

	ringbuffer_push(rb, atoms);

	return atoms;
}

/*
 * Add atoms to the ringbuffer - We are using ringbuffer_add_atoms which
 * will not know about wrapping. As we know we call it twice and summ up
 * the result.
 *
 */
int ringbuffer_add(struct ringbuffer_s *rb, uint8_t *atom, int num) {
	int		atoms;

	atoms=ringbuffer_add_atoms(rb, atom, num);
	if (atoms >= num)
		return atoms;

	atoms+=ringbuffer_add_atoms(rb, &atom[atoms*rb->atomsize], num-atoms);

	return atoms;
}

void ringbuffer_free(struct ringbuffer_s *rb) {
	free(rb->buffer),
	free(rb);
}

#ifdef TEST

#include <stdio.h>

void dump_hex(char *prefix, uint8_t *buf, int size) {
	int		i;
	unsigned char	ch;
	char		sascii[17];
	char		linebuffer[16*4+1];

	sascii[16]=0x0;

	for(i=0;i<size;i++) {
		ch=buf[i];
		if (i%16 == 0) {
			sprintf(linebuffer, "%04x ", i);
		}
		sprintf(&linebuffer[(i%16)*3], "%02x ", ch);
		if (ch >= ' ' && ch <= '}')
			sascii[i%16]=ch;
		else
			sascii[i%16]='.';

		if (i%16 == 15)
			printf("%s %s  %s\n", prefix, linebuffer, sascii);
	}

	/* i++ after loop */
	if (i%16 != 0) {
		for(;i%16 != 0;i++) {
			sprintf(&linebuffer[(i%16)*3], "   ");
			sascii[i%16]=' ';
		}

		printf("%s %s  %s\n", prefix, linebuffer, sascii);
	}
}


void ringbuffer_dump(struct ringbuffer_s *rb) {
	printf("Atoms: %d AtomSize: %d Head: %d Tail: %d Used: %d\n", 
			rb->atoms, rb->atomsize, rb->head, rb->tail, rb->used);
	dump_hex(" ", rb->buffer, rb->atoms*rb->atomsize);
	printf("\n");
}

int main(void ) {
	struct ringbuffer_s	*rb=ringbuffer_init(7, 1);
	int			i, j;
	uint8_t			add[2];

	for(i=1;i<=5;i++) {
		add[0]=i;
		add[1]=i;
		ringbuffer_dump(rb);
		printf("Trying to add 2 bytes... \n");
		j=ringbuffer_add(rb, add, 2);
		printf("... returned j=%d\n", j);
		ringbuffer_dump(rb);
	}
}

#endif
