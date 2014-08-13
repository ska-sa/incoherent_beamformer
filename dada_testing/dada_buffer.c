#include "ipcbuf.h"
#include "ipcio.h"
#include "multilog.h"
#include "dada_def.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* EDT recommends datarate/20 for bufsize, expected datarate 0f 6.7 Gbps */
#define BUFSIZE 4500000
#define DADA_KEY 0x1234
#define NUM_READERS 1
#define NBUFS 1

int dada_key;
int num_readers;
u_int64_t nbufs;
u_int64_t bufsz;

ipcbuf_t data_block = IPCBUF_INIT;
ipcbuf_t header = IPCBUF_INIT;


int page = 0;
int destroy = 0;
int lock = 0;
int arg;


int main (int argc, char **argv)
{
    uint64_t nbufs = DADA_DEFAULT_BLOCK_NUM;
    uint64_t bufsz = DADA_DEFAULT_BLOCK_SIZE;

    key_t dada_key = DADA_DEFAULT_BLOCK_KEY;


    fprintf(stderr, "------------------STARTING TEST---------------------\n");
    fprintf(stderr, "-----------------CREATING BUFFER--------------------\n"
            "dada_key = %d\n"
            "num_readers = %d\n"
            "nbufs = %"PRIu64"\n"
            "bufsz = %"PRIu64"\n",
            DADA_KEY, NUM_READERS, NBUFS, BUFSIZE);

    if (create_buffer(DADA_KEY, NUM_READERS, NBUFS, BUFSIZE) == 0)
    {
        fprintf (stderr, "-----------------BUFFER CREATED------------------\n");
    }
    else
    {
        fprintf (stderr, "FAILED TO CREATE BUFFER\n");
    }

    char *input;
    input = (char *) malloc(4);
    strcpy(input, "test");

    fprintf(stderr, "-------------------WRITING %s TO BUFFER----------------\n", input);
    if (write_to_buf(input) < 0)
        fprintf(stderr, "FAILED TO WRITE\n");
    else
        fprintf(stderr, "WROTE SUCCESSFULLY\n");
}

int write_to_buf (char *data, uint64_t size)
{
    uint64_t written = 0;
    ipcbuf_lock_write(&data_block);

    /* write data to datablock */
    written = ipcio_write(&data_block, data, size);
    fprintf (stderr, "Wrote %"PRIu64"bytes to buffer\n");

    ipcbuf_mark_filled (&data_block, written);

    ipcbuf_unlock_write (&data_block);

    if (written == size)
        return written;
    else
        return -1;
}

int delete_buffer ()
{

    ipcbuf_connect (&data_block, dada_key);
    ipcbuf_destroy (&data_block);

    ipcbuf_connect (&header, dada_key + 1);
    ipcbuf_destroy (&header);

    fprintf (stderr, "Destroyed DADA data and header blocks\n");

    return 0;
}

int create_buffer(int d_k, int n_r, u_int64_t nb, u_int64_t bs)
{
    uint64_t nhdrs = IPCBUF_XFERS;
    uint64_t hdrsz = DADA_DEFAULT_HEADER_SIZE;


    int dada_key = d_k;
    int num_readers = n_r;
    u_int64_t nbufs = nb;
    u_int64_t bufsz = bs;

    if ((num_readers < 1) || (num_readers > 5))
    {
        fprintf (stderr, "Number of readers was not sensible: %d\n", num_readers);
        return -1;
    }

    if (ipcbuf_create (&data_block, dada_key, nbufs, bufsz, num_readers) < 0)
    {
        fprintf (stderr, "Could not create DADA data block\n");
        return -1;
    }

    fprintf (stderr, "Created DADA data block with"
             " nbufs=%"PRIu64" bufsz=%"PRIu64" nread=%d\n", nbufs, bufsz, num_readers);

    if (ipcbuf_create (&header, dada_key + 1, nhdrs, hdrsz, num_readers) < 0)
    {
        fprintf (stderr, "Could not create DADA header block\n");
        return -1;
    }

    fprintf (stderr, "Created DADA header block with nhdrs = %"PRIu64", hdrsz "
             "= %"PRIu64" bytes, nread=%d\n", nhdrs, hdrsz, num_readers);

    if (lock && ipcbuf_lock (&data_block) < 0)
    {
        fprintf (stderr, "Could not lock DADA data block into RAM\n");
        return -1;
    }

    if (lock && ipcbuf_lock (&header) < 0)
    {
        fprintf (stderr, "Could not lock DADA header block into RAM\n");
        return -1;
    }

    if (page && ipcbuf_page (&header) < 0)
    {
        fprintf (stderr, "Could not page DADA header block into RAM\n");
        return -1;
    }

    if (page && ipcbuf_page (&data_block) < 0)
    {
        fprintf (stderr, "Could not page DADA data block into RAM\n");
        return -1;
    }

    return 0;
}