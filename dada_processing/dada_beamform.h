//DADA stuff
#include "dada_hdu.h"
#include "dada_def.h"
#include "ascii_header.h"



#define DADA_BUF_1 0x1234
#define DADA_BUF_2 0x2345

#define F_ID_SPEAD_ID 4096
#define TIMESTAMP_SPEAD_ID 4097
#define DATA_SPEAD_ID 4098
#define DATA_LEN 2048
#define DADA_NUM_READERS 1
#define DADA_NUM_BUFS 8
#define NUM_HEAPS_PER_BUFFER 262144
#define NUM_HEAPS_PER_ORDER_BUF NUM_HEAPS_PER_BUFFER / 4

#define ACCUMULATE 256
#define N_CHANS 1024
#define N_POLS 2
#define TIMESTAMPS_PER_HEAP 4
#define BYTES_PER_SAMPLE 1
#define TIMESTAMP_INCREMENT 2048
#define EXPECTED_HEAP_LEN TIMESTAMPS_PER_HEAP * N_CHANS * N_POLS * BYTES_PER_SAMPLE

#define DEST_PORT 7654
#define DEST_IP "localhost"

void connect_to_buffer (dada_hdu_t ** hdu, unsigned int dada_buf_key);

void hexdump(unsigned char *buffer, unsigned long index, unsigned long width);

unsigned long long get_timestamp(dada_hdu_t * hdu);

void initial_header(dada_hdu_t * hdu);

int accumulate (char * incoming, int16_t* accumulated, uint64_t size);

void beamform (int16_t * acc1, int16_t * acc2, int16_t * beamformed, uint64_t num_vals);
