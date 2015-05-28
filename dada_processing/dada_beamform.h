//DADA stuff
#include "dada_hdu.h"
#include "dada_def.h"
#include "ascii_header.h"


#define DADA_BUF_1 0x1234
#define DADA_BUF_2 0x2345

#define ACCUMULATE 256
#define N_CHANS 1024
#define N_POLS 2
#define TIMESTAMPS_PER_HEAP 4
#define BYTES_PER_SAMPLE 1
#define TIMESTAMP_INCREMENT 2048

#define DEST_PORT 7654
#define DEST_IP "localhost"

void connect_to_buffer (dada_hdu_t ** hdu, unsigned int dada_buf_key);

void hexdump(unsigned char *buffer, unsigned long index, unsigned long width);

unsigned long long get_timestamp(dada_hdu_t * hdu);

void initial_header(dada_hdu_t * hdu);

int accumulate (char * incoming, uint16_t* accumulated, uint64_t size);

void beamform (u_int16_t * acc1, u_int16_t * acc2, u_int16_t * beamformed, uint64_t num_vals);