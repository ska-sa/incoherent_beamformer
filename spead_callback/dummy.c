#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#include <spead_api.h>


#define KRED  "\x1B[31m"
#define RESET "\033[0m"

#define NUM_WORKERS 4

#define F_ID_SPEAD_ID 4096
#define TIMESTAMP_SPEAD_ID 4097
#define DATA_SPEAD_ID 4098
#define DATA_LEN 2048
#define DADA_BUF_ID 0x1234
#define DADA_NUM_READERS 1
#define DADA_NUM_BUFS 16
#define NUM_HEAPS_PER_BUFFER 32 * 10 * NUM_WORKERS * 100

#define TS_PER_HEAP 1
#define CHANS_PER_HEAP 1024
#define POLARIZATIONS 2
#define BYTES_PER_SAMPLE 1
#define EXPECTED_HEAP_LEN TS_PER_HEAP * CHANS_PER_HEAP * POLARIZATIONS * BYTES_PER_SAMPLE

#define DADA_BUFFER_SIZE EXPECTED_HEAP_LEN * NUM_HEAPS_PER_BUFFER
#define ORDER_BUFFER_SIZE NUM_WORKERS * EXPECTED_HEAP_LEN * 32
#define ORDER_BUFFER_SEGMENT NUM_WORKERS * EXPECTED_HEAP_LEN * 8 
#define DROPPED_PACKET_BUFFER_SIZE NUM_WORKERS * 4

clock_t start;
uint64_t diff;
uint64_t num_bytes_transferred;

void spead_api_destroy(struct spead_api_module_shared *s, void *data)
{
    diff = clock() - start;
    int sec = diff / CLOCKS_PER_SEC;

    float b_p_s = num_bytes_transferred/sec;
    fprintf(stderr, KRED "Transfered %f Gigabits per second\n" RESET, b_p_s/1000000000 * 8);
    
}

void *spead_api_setup(struct spead_api_module_shared *s)
{

    num_bytes_transferred = 0;
    start = NULL;
}


int spead_api_callback(struct spead_api_module_shared *s, struct spead_item_group *ig, void *data)
{
    if (start == NULL){
        start = clock(), diff;
    }
    num_bytes_transferred += EXPECTED_HEAP_LEN;

    return 0;
}



int spead_api_timer_callback(struct spead_api_module_shared *s, void *data)
{
    
    return 0;
}
