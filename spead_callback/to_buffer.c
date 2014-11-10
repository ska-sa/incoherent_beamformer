#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>

#include "ipcutil.h"
#include <sys/shm.h>
#include <sys/sem.h>

#include <signal.h>

#include <dada_buffer.h>

#include <spead_api.h>

#include <time.h>

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
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

#define ORDER_BUFFER_KEY 0x10235
#define DROPPED_PACKET_BUFFER_KEY 0x10236
#define DPB_SEM_KEY 0x10237

typedef enum { false, true } bool;

char* buffer; //local pointer to current buffer
unsigned long long heap_count;
unsigned long long prior_ts; //local prior ts
unsigned long long prev_ts;
ipcbuf_t buf_conn;
bool connect_to = true;

char* order_buffer;
int* dropped_packet_buffer;
unsigned long long order_buffer_tail;
int o_b_sem_id;
struct sembuf sb;

int num_dada_copies;


clock_t start;
uint64_t diff;
uint64_t num_bytes_transferred;


struct snap_shot
{
    int master_pid;
    ipcbuf_t circular_buf;
    char* buffer; //global pointer to current buffer
    unsigned long long prior_ts; //first ts for this buffer
    int buffer_full;
    key_t dada_key;

    char* order_buffer;
    int ob_id;
    char* dropped_packet_buffer;
    int dpb_id;

    int order_buffer_read_pos;
    int o_b_sem_id;
    struct sembuf sb;
};

void spead_api_destroy(struct spead_api_module_shared *s, void *data)
{
    fprintf(stderr, "\nDELETING SPEAD CALLBACK\n");

    diff = clock() - start;
    int sec = diff / CLOCKS_PER_SEC;

    float b_p_s = num_bytes_transferred/sec;
    fprintf(stderr, KRED "Transfered %f Gigabits per second\n" RESET, b_p_s/1000000000 * 8);

     struct snap_shot *ss;

     lock_spead_api_module_shared(s);

    if ((ss = get_data_spead_api_module_shared(s)) != NULL)
    {
        delete_buffer(&ss->circular_buf);

        ipc_delete(ss->order_buffer, ss->ob_id);
        ipc_delete(ss->dropped_packet_buffer, ss->dpb_id);
        // sem_delete(ss->o_b_sem_id);

        
#ifdef DEBUG
        fprintf(stderr, "%s: PID [%d] destroyed spead_api_shared\n", __func__, getpid());
#endif
     }
    else
    {

 #ifdef DEBUG
         fprintf(stderr, "%s: PID [%d] spead_api_shared is clean\n", __func__, getpid());
 #endif

    }
    shared_free(ss, sizeof(struct snap_shot));
    clear_data_spead_api_module_shared(s);


    
    unlock_spead_api_module_shared(s);



}

void *spead_api_setup(struct spead_api_module_shared *s)
{
    fprintf(stderr,"\nCreating spead callback\n");

    fprintf (stderr, "SETTINGS\n");
    fprintf (stderr, "NUM_HEAPS_PER_BUFFER = %d\n", NUM_HEAPS_PER_BUFFER);
    fprintf (stderr, "EXPECTED_HEAP_LEN = %d\n", EXPECTED_HEAP_LEN);
    fprintf (stderr, "DADA_BUFFER_SIZE = %llu\n", DADA_BUFFER_SIZE);
    fprintf (stderr, "ORDER_BUFFER_SIZE = %d\n", ORDER_BUFFER_SIZE);
    fprintf (stderr, "ORDER_BUFFER_SEGMENT = %d\n", ORDER_BUFFER_SEGMENT);
    fprintf (stderr, "DROPPED_PACKET_BUFFER_SIZE = %d\n", DROPPED_PACKET_BUFFER_SIZE);


    struct snap_shot *ss;

    ss = NULL;

    lock_spead_api_module_shared(s);

    if (!(ss = get_data_spead_api_module_shared(s)))
    {
        ss = shared_malloc(sizeof(struct snap_shot));
        if (ss == NULL)
        {
            unlock_spead_api_module_shared(s);
            return NULL;
        }

        uint64_t d_b_s = DADA_BUFFER_SIZE;

        fprintf(stderr,"\nCREATING DADA BUFFER\n");
        create_buffer(& ss->circular_buf, DADA_BUF_ID, DADA_NUM_READERS, DADA_NUM_BUFS, d_b_s);
        ss->dada_key = DADA_BUF_ID;
        ss->buffer_full = 0;
        ss->prior_ts = 0;
        ss->master_pid = 0;

        fprintf(stderr, "\nCREATING ORDER BUFFERS\n");
         ss->order_buffer = ipc_alloc(ORDER_BUFFER_KEY, ORDER_BUFFER_SIZE, IPC_CREAT | IPC_EXCL | 0666, &ss->ob_id);
         ss->dropped_packet_buffer = ipc_alloc(DROPPED_PACKET_BUFFER_KEY, DROPPED_PACKET_BUFFER_SIZE, IPC_CREAT | IPC_EXCL | 0666, &ss->dpb_id);
        memset(ss->dropped_packet_buffer, 0, DROPPED_PACKET_BUFFER_SIZE);

        ss->sb.sem_num = 0;
        ss->sb.sem_op = -1;  /* set to allocate resource */
        ss->sb.sem_flg = SEM_UNDO;

        if ((ss->o_b_sem_id = initsem(DPB_SEM_KEY, 1)) == -1) {
            fprintf (stderr, "FAILED TO CREAT SEMAPHORE!");
        }

        set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
    }

    num_bytes_transferred = 0;

    unlock_spead_api_module_shared(s);

    printf("EXITING");
}

void set_bit(int* array, int pos)
{
    sb.sem_op = -1;  
    if (semop(o_b_sem_id, &sb, 1) == -1) {
        perror("semop");
        exit(1);
    }
    array[pos/32] |= 1 << (pos % 32);
    sb.sem_op = 1;
    if (semop(o_b_sem_id, &sb, 1) == -1) {
        perror("semop");
        exit(1);
    }

}

int get_bit(int* array, int pos)
{
    return ((array[pos/32] & (1 << (pos%32) )) != 0);
}

void write_to_order_buffer(char* heap, unsigned long long offset)
{
    unsigned long long o_b_s = ORDER_BUFFER_SIZE;
    int o_b_off = offset %  o_b_s;
    // fprintf(stderr, "[%d] -- WRITING TO ORDER BUFFER AT offset = %llu and dpo = %d\n ", getpid(),offset, o_b_off); 
    memcpy(order_buffer + o_b_off, heap, EXPECTED_HEAP_LEN);
    int e_h_l = EXPECTED_HEAP_LEN;
    int dp_pos = o_b_off/e_h_l;
    // fprintf(stderr, "[%d] -- WRITING TO DP BUFFER at o_b_off = %d, EXPECTED_HEAP_LEN = %llu and (llu) o_b_off/EXPECTED_HEAP_LEn = %llu (d) o_b_off/EXPECTED_HEAP_LEn = %d\n", getpid(), o_b_off, EXPECTED_HEAP_LEN, dp_pos, dp_pos);
    set_bit(dropped_packet_buffer, dp_pos);
    // fprintf(stderr, KBLU "[%d] -- dpb = %d" RESET , getpid(), dropped_packet_buffer[o_b_off / e_h_l / 32]);
}

void zero_dropped_packets()
{
    int e_h_l = EXPECTED_HEAP_LEN;
    unsigned long long o_b_s = ORDER_BUFFER_SIZE;
    size_t o_b_seg = ORDER_BUFFER_SEGMENT;
    int o_b_off = order_buffer_tail %  o_b_s;
    int i = 0;
    for (i = 0; i < 32; i++){
        if (get_bit(dropped_packet_buffer, o_b_off / e_h_l + i))
            memset(buffer + order_buffer_tail, 0, o_b_seg);
    }
}

void to_dada_buffer ()
{  
    unsigned long long o_b_s = ORDER_BUFFER_SIZE;
    int o_b_off = order_buffer_tail %  o_b_s;

    // fprintf(stderr, KBLU "[%d] -- WRITING TO DADA BUFFER from offset = %d to dada offset = %llu\n" RESET,getpid(), order_buffer_tail % o_b_s, order_buffer_tail);
    size_t o_b_seg = ORDER_BUFFER_SEGMENT;
    memcpy(buffer + order_buffer_tail, order_buffer + o_b_off, o_b_seg);
    unsigned long long d_b_s = DADA_BUFFER_SIZE;
    

    int e_h_l = EXPECTED_HEAP_LEN;

    if (dropped_packet_buffer[o_b_off / e_h_l / 32] == -1){
        // fprintf(stderr, KGRN "[%d] -- No dropped packets. dpb = %d\n" RESET , getpid(), dropped_packet_buffer[o_b_off / e_h_l / 32]);
    }
    else{
        // fprintf(stderr, KRED "[%d] -- Dropped packets. dpb = %d\n" RESET, getpid(), dropped_packet_buffer[o_b_off / e_h_l / 32]);
        zero_dropped_packets();
    }

    dropped_packet_buffer[o_b_off / e_h_l / 32] = 0;
    order_buffer_tail = (order_buffer_tail + ORDER_BUFFER_SEGMENT) % d_b_s;
    
    num_bytes_transferred += ORDER_BUFFER_SEGMENT;

}




int spead_api_callback(struct spead_api_module_shared *s, struct spead_item_group *ig, void *data)
{
    // fprintf(stderr,"\n[%d] -- accessing spead packet --------------------------\n",getpid());
    struct snap_shot *ss;
    struct spead_api_item *itm;
    unsigned long long ts;
    unsigned long long offset;

    ss = get_data_spead_api_module_shared(s); //Get shared data module
    itm = NULL;

    itm = get_spead_item_with_id(ig, TIMESTAMP_SPEAD_ID);

    if (itm == NULL){
    // fprintf(stderr, "%s: No timestamp data found (id: 0x%x)\n", __func__, TIMESTAMP_SPEAD_ID);
    return -1;
    }
    // TS is 5 byte unsigned 
    ts = (long long)itm->i_data[0] + (long long)itm->i_data[1] * 256 + (long long)itm->i_data[2] * 256 * 256 + (long long)itm->i_data[3] * 256 * 256 * 256 + (long long)itm->i_data[4] * 256 * 256 * 256 * 256;


    itm = get_spead_item_with_id(ig, DATA_SPEAD_ID);
    if (itm == NULL){
        fprintf(stderr, "%s: No beamformer payload data found.\n", __func__); 
        return -1;
    }
    // check that the heap size matches our expectations
    if (itm->i_data_len != EXPECTED_HEAP_LEN) {
       fprintf(stderr,"%s: Expecting heap size of %i, got %llu\n",__func__,EXPECTED_HEAP_LEN, itm->i_data_len);
       return -1;
    }

    lock_spead_api_module_shared(s); //lock shared data

    if (ss == NULL)
    {
        fprintf(stderr, "ss is null\n");
        unlock_spead_api_module_shared(s);
        //Shared resources empty, unlock and return
        return -1;
    }

    if (ss->prior_ts == 0) { //First thread to run
        start = clock(), diff;
        if (ss->master_pid == 0){
            ss->master_pid = getpid();
            // make sure we know who is boss
            ss->prior_ts = ts;
            ss->buffer=get_next_buf(& ss->circular_buf);
            set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
            // fprintf(stderr, "[%d] -- ts = %llu, local prior_ts = %llu, global prior_ts = %llu\n",getpid(),ts,prior_ts,ss->prior_ts);
            buffer = ss->buffer;
            connect_to_buffer(&buf_conn, ss->dada_key);
            num_dada_copies = 0;
        }
    }
    unlock_spead_api_module_shared(s);

    if (order_buffer == NULL){ //Must be first time non master thread has run
        // fprintf (stderr, "[%d] -- NULL BUFFER\n",getpid());
        prior_ts = ss->prior_ts;

        order_buffer = (char *)shmat(ss->ob_id, NULL, 0);
        dropped_packet_buffer = (char *)shmat(ss->dpb_id, NULL, 0);
        sb = ss->sb;
        o_b_sem_id = ss->o_b_sem_id;
        order_buffer_tail = 0;
    }

    if (prior_ts != ss->prior_ts)
        prior_ts = ss->prior_ts;

    offset = (ts - prior_ts) / 512 * EXPECTED_HEAP_LEN;


    write_to_order_buffer(itm, offset);  //Copy data to buffer

    // fprintf(stderr,"[%d] -- WRITTEN ----------------------------\n", getpid());

    long long o_b_s = ORDER_BUFFER_SIZE;
    long long limit = ORDER_BUFFER_SEGMENT;
    long long check = offset - order_buffer_tail;
    uint64_t d_b_s = DADA_BUFFER_SIZE;

    

    if (getpid() == ss->master_pid){
        if (( check > limit) || ((check) * -1 > limit))
        {
            
            if (offset >= d_b_s){
                lock_spead_api_module_shared(s);
                // fprintf(stderr,KYEL "[%d] -- Trigger new buffer...----------------\n" RESET,getpid());
                mark_filled(& ss->circular_buf, d_b_s);
                ss->buffer=get_next_buf(& ss->circular_buf);
                unsigned long long add = NUM_HEAPS_PER_BUFFER * 512;
                ss->prior_ts = (ss->prior_ts + add) % ULLONG_MAX;
                set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
                
            
                buffer = ss->buffer;
                prior_ts = ss->prior_ts;
                offset = (ts - prior_ts) / 512 * EXPECTED_HEAP_LEN;
                set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
                unlock_spead_api_module_shared(s);
            }

            
            to_dada_buffer();
            // fprintf(stderr, KBLU "[%d] -- num_dada_copies  = %d -----------------\n" RESET,getpid(),num_dada_copies);
        }
    }
    prev_ts = ts;

    return 0;
}

int spead_api_timer_callback(struct spead_api_module_shared *s, void *data)
{
    return 0;
}