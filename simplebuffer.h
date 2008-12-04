
void *sb_init(int atoms, int atomsize, int headroom);
void sb_free(void *sbv);
int sb_used_atoms(void *sbv);
int sb_free_atoms(void *sbv);
int sb_add_atoms(void *sbv, uint8_t *atom, int atoms);
void sb_zap(void *sbv);
uint8_t *sb_bufptr(void *sbv);
int sb_buflen(void *sbv);
