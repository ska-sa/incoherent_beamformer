#include "dada_hdu.h"
#include "dada_def.h"
#include "ascii_header.h"

/* #define _DEBUG 1 */
#define MAX_FILES 1024

// void usage();

// function to write the header to the datablock
int simple_writer_open (dada_hdu_t * hdu);

//
// extremely simple (non useful) code to write to the data block
// the techniques below use the simple or the efficient interface
// to the ring buffer
//
int simple_writer_write (dada_hdu_t * hdu, char * input, uint64_t block_size);

void simple_writer_connect_hdu (dada_hdu_t * hdu, key_t dada_key);
void simple_writer_close_hdu (dada_hdu_t * hdu, uint64_t block_size);