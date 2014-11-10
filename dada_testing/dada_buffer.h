#include "ipcbuf.h"
#include "ipcio.h"
#include "multilog.h"
#include "dada_def.h"

#include "ipcutil.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* EDT recommends datarate/20 for bufsize, expected datarate 0f 6.7 Gbps */
#define BUFSIZE 3252
#define DADA_KEY 0x1234
#define NUM_READERS 1
#define NBUFS 8
#define MAX_WRITERS 8

void usage ();

char *mark_filled_and_get_next_buf(ipcbuf_t* , uint64_t pos);

int read_next_buf(ipcbuf_t*, char **out);

void mark_filled (ipcbuf_t*, uint64_t pos);

void set_current_buf(ipcbuf_t*, char *new_buf);

char *get_next_buf (ipcbuf_t*);

int write_to_buf (ipcbuf_t*, char* buffer, char *data, uint64_t size);

int delete_buffer (ipcbuf_t*);

int connect_to_buffer(ipcbuf_t*, key_t key);

/*Create a buffer*/
int create_buffer(ipcbuf_t*, key_t d_k, int n_r, u_int64_t nb, u_int64_t bs);

int lock_buffer(ipcbuf_t*);

void consume (ipcbuf_t*);

void produce (ipcbuf_t*);