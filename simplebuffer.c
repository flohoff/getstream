#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/param.h>

struct simplebuffer_s {
	uint8_t		*buffer;
	int		atomsize;
	int		atoms;
	int		fill;
	int		headroom;
};

void *sb_init(int atoms, int atomsize, int headroom) {
	struct simplebuffer_s	*sb;

	sb=calloc(1, sizeof(struct simplebuffer_s));
	if (!sb)
		return NULL;


	sb->buffer=malloc(atoms*atomsize+headroom);
	if (!sb->buffer) {
		free(sb);
		return NULL;
	}

	sb->atoms=atoms;
	sb->atomsize=atomsize;
	sb->headroom=headroom;

	return sb;
}

void sb_free(void *sbv) {
	struct simplebuffer_s *sb=sbv;
	free(sb->buffer);
	free(sb);
}

int sb_used_atoms(void *sbv) {
	struct simplebuffer_s *sb=sbv;
	return sb->fill;
}

int sb_free_atoms(void *sbv) {
	struct simplebuffer_s *sb=sbv;
	return (sb->atoms-sb->fill);
}

int sb_add_atoms(void *sbv, uint8_t *atom, int atoms) {
	struct simplebuffer_s *sb=sbv;
	int	copy;

	copy=MIN(atoms, sb_free_atoms(sbv));
	memcpy(&sb->buffer[sb->fill*sb->atomsize+sb->headroom], atom, copy*sb->atomsize);
	sb->fill+=copy;

	return copy;
}

uint8_t *sb_bufptr(void *sbv) {
	struct simplebuffer_s *sb=sbv;
	return sb->buffer;
}

int sb_buflen(void *sbv) {
	struct simplebuffer_s *sb=sbv;
	return sb->fill*sb->atomsize+sb->headroom;
}

void sb_zap(void *sbv) {
	struct simplebuffer_s *sb=sbv;
	sb->fill=0;
}
