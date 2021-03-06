#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/gmon.h>
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

int count = 0;
clock_t start;
uint64_t diff;
uint64_t num_bytes_transferred;
struct snap_shot
{
    unsigned int numheaps;
};

void spead_api_destroy(struct spead_api_module_shared *s, void *data)
{
    fprintf (stderr, "DESTROY");
    //gmon_start_all_thread_timers();
    //gmon_thread_timer(1);
    // diff = clock() - start;
    // int sec = diff / CLOCKS_PER_SEC;

    // float b_p_s = num_bytes_transferred/sec;
    // fprintf(stderr, KRED "Transfered %f Gigabits per second\n" RESET, b_p_s/1000000000 * 8);

    // lock_spead_api_module_shared(s);

    // struct snap_shot *ss;

    // ss = NULL;

    // ss = get_data_spead_api_module_shared(s);

    // fprintf(stderr, KRED "numheaps = %u\n" RESET, ss->numheaps);
    
}

void *spead_api_setup(struct spead_api_module_shared *s)
{
    //gmon_thread_timer(1);
    fprintf(stderr, "IN SETUP\n");
    // struct snap_shot *ss;

    // ss = NULL;
   

    // lock_spead_api_module_shared(s);
    // fprintf(stderr, "LOCKED\n");

    // if (!(ss = get_data_spead_api_module_shared(s)))
    // {
    //     fprintf(stderr, "ALLOCATING\n");
    //     ss = shared_malloc(sizeof(struct snap_shot));
    //     if (ss == NULL)
    //     {
    //         unlock_spead_api_module_shared(s);
    //         return NULL;
    //     }
    //     ss->numheaps = 0;
    // }
    // fprintf(stderr, "ALLOCATED\n");
    // set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));

    // unlock_spead_api_module_shared(s);

    // num_bytes_transferred = 0;
    // start = NULL;
    fprintf(stderr, "OUT SETUP\n");
}


int spead_api_callback(struct spead_api_module_shared *s, struct spead_item_group *ig, void *data)
{
    //fprintf(stderr, "IN CALLBACK\n");
    // if (start == NULL)
    //     start = clock(), diff;
    // if (count > 100000000)
    //     exit(0);

    //fprintf(stderr, "YOLO");
    // count++;
    // struct snap_shot *ss;

    // ss = get_data_spead_api_module_shared(s); 
    // lock_spead_api_module_shared(s);
    // ss->numheaps +=1;
    // set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
    // unlock_spead_api_module_shared(s);

    // num_bytes_transferred += EXPECTED_HEAP_LEN;

    return 0;
}



int spead_api_timer_callback(struct spead_api_module_shared *s, void *data)
{
    
    return 0;
}
