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

//dada
#include "ipcbuf.h"
#include "ipcio.h"
#include "multilog.h"
#include "dada_def.h"

#include "ipcutil.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


// #include <dada_buffer.h>

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
#define DADA_NUM_BUFS 8
#define NUM_HEAPS_PER_BUFFER 32
#define NUM_HEAPS_PER_ORDER_BUF 8

#define TIMESTAMPS_PER_HEAP 1
#define TIMESTAMP_INCREMENT 512
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
#define DROPPED_PACKET_BUFFER_SEM_KEY 0x10237

//SPEAD variables
unsigned int numWorkers;
unsigned int fIdSpeadId;
unsigned int timestampSpeadId;
unsigned int timestampIncrement;
unsigned int dataSpeadId;
unsigned int timestampsPerHeap;
unsigned int chanPerHeap;
unsigned int pols;
unsigned int bytesPerSample;
unsigned long long expectedHeapLen;

//DADA variables
unsigned int dadaBufId;
unsigned int dadaNumReaders;
unsigned int dadaNumBufs;
size_t numHeapsPerBuf;
size_t dadaBufSize;

//Order buffer variables
unsigned int numHeapsPerOB;
unsigned long long obSize;
unsigned long long obSegment;
unsigned int obKey; 

//Dropped packet buffer size
unsigned int dpbSize;
unsigned int dpbKey;
unsigned int dbpSemKey;

void get_settings()
{
    char * env_temp;
    env_temp = getenv("NUM_WORKERS");  //Number of speadrx workers
    if (env_temp != NULL) numWorkers = atoi(env_temp);
    else numWorkers = NUM_WORKERS;

    env_temp = getenv("F_ID_SPEAD_ID");
    if (env_temp != NULL) fIdSpeadId = atoi(env_temp);
    else fIdSpeadId = F_ID_SPEAD_ID;

    env_temp = getenv("TIMESTAMP_SPEAD_ID");
    if (env_temp != NULL) timestampSpeadId = atoi(env_temp);
    else timestampSpeadId = TIMESTAMP_SPEAD_ID;

    env_temp = getenv("DATA_SPEAD_ID");
    if (env_temp != NULL) dataSpeadId = atoi(env_temp);
    else dataSpeadId = DATA_SPEAD_ID;

    env_temp = getenv("TIMESTAMPS_PER_HEAP");
    if (env_temp != NULL) timestampsPerHeap = atoi(env_temp);
    else timestampsPerHeap = TIMESTAMPS_PER_HEAP;

    env_temp = getenv("TIMESTAMP_INCREMENT");
    if (env_temp != NULL) timestampIncrement = atoi(env_temp);
    else timestampIncrement = TIMESTAMP_INCREMENT;

    env_temp = getenv("CHANS_PER_HEAP");
    if (env_temp != NULL) chanPerHeap = atoi(env_temp);
    else chanPerHeap = CHANS_PER_HEAP;

    env_temp = getenv("POLARIZATIONS");
    if (env_temp != NULL) pols = atoi(env_temp);
    else pols = POLARIZATIONS;

    env_temp = getenv("BYTES_PER_SAMPLE");
    if (env_temp != NULL) bytesPerSample = atoi(env_temp);
    else bytesPerSample = BYTES_PER_SAMPLE;

    expectedHeapLen = timestampsPerHeap * chanPerHeap * pols * bytesPerSample;

    env_temp = getenv("DADA_BUF_ID"); //ID of dada buffer
    if (env_temp != NULL) dadaBufId = atoi(env_temp);
    else dadaBufId = DADA_BUF_ID;

    env_temp = getenv("DADA_NUM_READERS");
    if (env_temp != NULL) dadaNumReaders = atoi(env_temp);
    else dadaNumReaders = DADA_NUM_READERS;

    env_temp = getenv("DADA_NUM_BUFS");
    if (env_temp != NULL) dadaNumBufs = atoi(env_temp);
    else dadaNumBufs = DADA_NUM_BUFS;

    // env_temp = getenv("BYTES_PER_SAMPLE");
    // if (env_temp != NULL) bytesPerSample = atoi(env_temp);
    // else bytesPerSample = BYTES_PER_SAMPLE;

    // #define NUM_HEAPS_PER_BUFFER 32
    // #define NUM_HEAPS_PER_ORDER_BUF 8

    env_temp = getenv("NUM_HEAPS_PER_BUFFER");
    if (env_temp != NULL) numHeapsPerBuf = atoi(env_temp);
    else numHeapsPerBuf = NUM_HEAPS_PER_BUFFER;

    env_temp = getenv("NUM_HEAPS_PER_ORDER_BUF");
    if (env_temp != NULL) numHeapsPerOB = atoi(env_temp);
    else numHeapsPerOB = NUM_HEAPS_PER_BUFFER;

    if (numHeapsPerBuf % numHeapsPerOB != 0)
    {
        fprintf(stderr, "Number of heaps per DADA buffer = %u\n Not divisible by num heaps per order buffer = %u",numHeapsPerBuf, numHeapsPerOB);
        exit(0);
    }

    // numHeapsPerBuf = 32 * numWorkers; //Number of heaps to put in each Dada buffer

    fprintf(stderr, "numheapsperbuff = %zu\n", numHeapsPerBuf);

    dadaBufSize = expectedHeapLen * numHeapsPerBuf;

    fprintf(stderr, "dadaBufSize = %zu\n", dadaBufSize);

    obSize = numHeapsPerOB * expectedHeapLen;

    if (numHeapsPerOB % 4 != 0){
        fprintf(stderr, "number of heaps in order buffer = %u, should be divisible by 4");
        exit(0);
    }

    obSegment = obSize / 4;

    env_temp = getenv("ORDER_BUFFER_KEY");
    if (env_temp != NULL) obKey = atoi(env_temp);
    else obKey = ORDER_BUFFER_KEY;

    dpbSize = numHeapsPerOB;

    env_temp = getenv("DROPPED_PACKET_BUFFER_KEY");
    if (env_temp != NULL) dpbKey = atoi(env_temp);
    else dpbKey = DROPPED_PACKET_BUFFER_KEY;

    env_temp = getenv("DROPPED_PACKET_BUFFER_SEM_KEY");
    if (env_temp != NULL) dbpSemKey = atoi(env_temp);
    else dbpSemKey = DROPPED_PACKET_BUFFER_SEM_KEY;

}

typedef enum { false, true } bool;

char* buffer; //local pointer to current buffer
unsigned long long prior_ts; //local prior ts

char* order_buffer;                     //buffer to reorder/zero heaps
int* dropped_packet_buffer;             //bit array to store whether a heap has been dropped
unsigned long long order_buffer_tail;   //Keeps track of the total number of heaps
int o_b_sem_id;                         //order buffer sempahore key
struct sembuf sb;                       //oreder buffer semaphore

// int num_dada_copies;

clock_t start;
uint64_t diff;
uint64_t num_bytes_transferred;

struct snap_shot
{
    int master_pid;
    ipcbuf_t circular_buf;
    char* buffer; //global pointer to current buffer (NOT NECESSARY?)
    unsigned long long prior_ts; //first ts for this buffer
    int buffer_created;  //Is the buffer full (NOTNECESSARY?)
    key_t dada_key;   //Dada key (NOT NECESSARY?)

    char* order_buffer;             //Order buffer (Not necessary, make local copy, only keep keys here)
    int ob_id;                      //SHMEM key for order buffer
    char* dropped_packet_buffer;    //Dropped packet buffer (NOT NECESARY?)
    int dpb_id;                     //Dropped packet shmem key

    int order_buffer_read_pos;      //Next read position of order buffer
    int o_b_sem_id;                 //Order buffer semaphore key
    struct sembuf sb;               //order buffer semaphore (not necessary make local copy?)
};

void spead_api_destroy(struct spead_api_module_shared *s, void *data)
{
    fprintf(stderr, "\nDELETING SPEAD CALLBACK\n");

    diff = clock() - start;
    int sec = diff / CLOCKS_PER_SEC;

    float b_p_s = num_bytes_transferred/sec;
    fprintf(stderr, KRED "Transfered %f Gigabits per second\n" RESET, b_p_s/1000000000 * 8);

    fprintf(stderr, ": PID [%d] master PID = \n",getpid());
    struct snap_shot *ss;
    // fprintf(stderr, "%s: PID [%d] master PID = %d\n",getpid(), ss->master_pid);
    ss = get_data_spead_api_module_shared(s);
    fprintf(stderr, ": PID [%d] master PID = %d\n",getpid(), ss->master_pid);
    if ((ss != NULL) && (getpid() == ss->master_pid + 1)){

        lock_spead_api_module_shared(s);

        // delete_buffer(&ss->circular_buf);
        ipc_delete(ss->order_buffer, ss->ob_id);
        ipc_delete(ss->dropped_packet_buffer, ss->dpb_id);
        sem_delete(ss->o_b_sem_id);

        shared_free(ss, sizeof(struct snap_shot));
        clear_data_spead_api_module_shared(s);
        unlock_spead_api_module_shared(s);
        fprintf(stderr, "%s: PID [%d] destroyed spead_api_shared\n", __func__, getpid());

#ifdef DEBUG
        fprintf(stderr, "%s: PID [%d] destroyed spead_api_shared\n", __func__, getpid());
        fprintf(stderr, "%s: PID [%d] spead_api_shared is clean\n", __func__, getpid());
 #endif
    
    }
}

void *spead_api_setup(struct spead_api_module_shared *s)
{
    get_settings();

    fprintf(stderr,"\nCreating spead callback\n");

    fprintf (stderr, "SETTINGS\n");
    fprintf (stderr, "NUM_HEAPS_PER_BUFFER = %llu\n", numHeapsPerBuf);
    fprintf (stderr, "EXPECTED_HEAP_LEN = %llu\n", expectedHeapLen);
    fprintf (stderr, "DADA_BUFFER_SIZE = %llu\n", dadaBufSize);
    fprintf (stderr, "ORDER_BUFFER_SIZE = %llu\n", obSize);
    fprintf (stderr, "ORDER_BUFFER_SEGMENT = %llu\n", obSegment);
    fprintf (stderr, "DROPPED_PACKET_BUFFER_SIZE = %d\n", dpbSize);

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

        ss->dada_key = dadaBufId;
        // ss->buffer_full = 0;
        ss->prior_ts = 0;
        // ss->master_pid = 0;

        if (ss->buffer_created == 0){
            // ss->master_pid = getpid();
            
            // make sure we know who is boss

            connect_to_buffer(&ss->circular_buf, ss->dada_key);
            ss->buffer_created == 1;
            // ss->prior_ts = ts;
            // ss->buffer=get_next_buf(& ss->circular_buf);
            // set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
            // buffer = ss->buffer;
            
            // num_dada_copies = 0;

            fprintf(stderr, "\nCREATING ORDER BUFFERS\n");
             ss->order_buffer = ipc_alloc(obKey, obSize, IPC_CREAT | IPC_EXCL | 0666, &ss->ob_id);
             ss->dropped_packet_buffer = ipc_alloc(dpbKey, dpbSize, IPC_CREAT | IPC_EXCL | 0666, &ss->dpb_id);
            memset(ss->dropped_packet_buffer, 0, dpbSize);

            ss->sb.sem_num = 0;
            ss->sb.sem_op = -1;  /* set to allocate resource */
            ss->sb.sem_flg = SEM_UNDO;

            if ((ss->o_b_sem_id = initsem(dbpSemKey, 1)) == -1) {
                fprintf (stderr, "FAILED TO CREATE SEMAPHORE!");
            }
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
    // fprintf(stderr, "WRITE TO ORDER BUFFER\n");
    unsigned long long o_b_off = offset %  obSize;
    memcpy(order_buffer + o_b_off, heap, expectedHeapLen);
    int dp_pos = o_b_off/expectedHeapLen;
    set_bit(dropped_packet_buffer, dp_pos);
    // fprintf(stderr, "EXIT WRITE TO ORDER BUFFER\n");
}

void zero_dropped_packets()  //Should only be called by master thread
{
    int o_b_off = order_buffer_tail %  obSize;
    int i = 0;
    for (i = 0; i < 32; i++){
        if (get_bit(dropped_packet_buffer, o_b_off / expectedHeapLen + i))
            memset(buffer + order_buffer_tail, 0, obSegment);
    }
}

void to_dada_buffer ()  //SHould only be called by master thread
{  
    fprintf(stderr, "IN to dada buffer\n");
    int o_b_off = order_buffer_tail %  obSize;
    if (buffer == NULL)
    {
        fprintf(stderr, "NO BUFFER\n");
    }
    if (order_buffer == NULL)
        fprintf(stderr, "NO ORDER BUFFER\n");

    fprintf(stderr, "order_buffer_tail = %llu, o_b_off = %d, obSegment = %d\n", order_buffer_tail, o_b_off, obSegment);
    memcpy(buffer + order_buffer_tail, order_buffer + o_b_off, obSegment);
    fprintf(stderr, "after copy");

    if (dropped_packet_buffer[o_b_off / expectedHeapLen / 32] == -1){
         fprintf(stderr, KGRN "[%d] -- No dropped packets. dpb = %d\n" RESET , getpid(), dropped_packet_buffer[o_b_off / expectedHeapLen / 32]);
    }
    else{
        fprintf(stderr, KRED "[%d] -- Dropped packets. dpb = %d\n" RESET, getpid(), dropped_packet_buffer[o_b_off / expectedHeapLen / 32]);
        zero_dropped_packets();
    }

    dropped_packet_buffer[o_b_off / expectedHeapLen / 32] = 0;
    order_buffer_tail = (order_buffer_tail + obSegment) % dadaBufSize;
    num_bytes_transferred += obSegment;
}




int spead_api_callback(struct spead_api_module_shared *s, struct spead_item_group *ig, void *data)
{
    struct snap_shot *ss;
    struct spead_api_item *itm;
    unsigned long long ts;
    unsigned long long offset;

    ss = get_data_spead_api_module_shared(s); //Get shared data module
    itm = NULL;

    itm = get_spead_item_with_id(ig, timestampSpeadId);

    if (itm == NULL){
     fprintf(stderr, "%s: No timestamp data found (id: 0x%x)\n", __func__, TIMESTAMP_SPEAD_ID);
    return -1;
    }
    // TS is 5 byte unsigned 
    ts = (long long)itm->i_data[0] + (long long)itm->i_data[1] * 256 + (long long)itm->i_data[2] * 256 * 256 + (long long)itm->i_data[3] * 256 * 256 * 256 + (long long)itm->i_data[4] * 256 * 256 * 256 * 256;


    itm = get_spead_item_with_id(ig, dataSpeadId);
    if (itm == NULL){
        fprintf(stderr, "%s: No beamformer payload data found.\n", __func__); 
        return -1;
    }

    // check that the heap size matches our expectations
    if (itm->i_data_len != expectedHeapLen) {
       fprintf(stderr,"%s: Expecting heap size of %llu, got %llu\n", __func__, expectedHeapLen, itm->i_data_len);
       return -1;
    }

     //lock shared data

    if (ss == NULL)
    {
        fprintf(stderr, "ss is null\n");
        unlock_spead_api_module_shared(s);
        //Shared resources empty, unlock and return
        return -1;
    }

    // if ( (ss->master_pid == getpid()) && (buffer == NULL) ){
    //         // ss->master_pid = getpid();
    //         // make sure we know who is boss

    //         // connect_to_buffer(&ss->circular_buf, ss->dada_key);
            
            
    //         // num_dada_copies = 0;
    //     }

    if (ss->prior_ts == 0) { //First thread to run
        start = clock(), diff;
        lock_spead_api_module_shared(s);
        ss->master_pid = getpid();
        fprintf(stderr, KGRN "MASTER thread is %d\n" RESET, getpid());
        ss->prior_ts = ts;
            
        ss->buffer=get_next_buf(& ss->circular_buf);
        fprintf(stderr, "next buff");
        ss->buffer=get_next_buf(& ss->circular_buf);
        buffer = ss->buffer;
        set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));

        unlock_spead_api_module_shared(s);
        unlock_spead_api_module_shared(s);
    }
    

    if (order_buffer == NULL){ //Must be first time non master thread has run
        prior_ts = ss->prior_ts;

        order_buffer = (char *)shmat(ss->ob_id, NULL, 0);
        dropped_packet_buffer = (char *)shmat(ss->dpb_id, NULL, 0);
        sb = ss->sb;
        o_b_sem_id = ss->o_b_sem_id;
        order_buffer_tail = 0;
    }

    if (prior_ts > ss->prior_ts)
        prior_ts = ss->prior_ts;
    else if (prior_ts < ss->prior_ts)
    {

        fprintf(stderr, KRED "prior_ts = %llu, ss-> prior_ts = %llu\nHeap too late, dropped heap" RESET, prior_ts, ss->prior_ts);
    }

    offset = (ts - prior_ts) / timestampIncrement * expectedHeapLen;


    fprintf (stderr, KYEL "[%d] prior_ts = %llu, ss-> prior_ts = %llu\n"RESET, getpid(), prior_ts, ss->prior_ts);
    fprintf (stderr, KYEL "[%d] ts = %llu\n"RESET, getpid(), ts);
    fprintf (stderr, KYEL "[%d] offset = %llu\n"RESET, getpid(), offset);

    write_to_order_buffer(itm, offset);  //Copy data to buffer
    long long check = offset - order_buffer_tail; //If this difference is greater than a orderbuffer segment, then we should move data to dada buffer

    fprintf (stderr, KYEL "[%d] check = %llu, obSegment = %llu\n" RESET , getpid(), check, obSegment);

    if (getpid() == ss->master_pid){  //Only the master thread deals with the DADA buffer

        if (( check > obSegment) || ((check) * -1 > obSegment))
        {
            
            if (offset >= dadaBufSize){  //If the offset is greater than a DADA buffer, we need to open a new buffer and reset counters

                lock_spead_api_module_shared(s);
                mark_filled(& ss->circular_buf, dadaBufSize);
                ss->buffer=get_next_buf(& ss->circular_buf);
                unsigned long long add = numHeapsPerBuf * timestampIncrement; //How much to add to the timstamp
                ss->prior_ts = (ss->prior_ts + add) % ULLONG_MAX;  //New prior_ts
                set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
            
                buffer = ss->buffer;
                prior_ts = ss->prior_ts;
                offset = (ts - prior_ts) / timestampIncrement * expectedHeapLen;
                set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
                unlock_spead_api_module_shared(s);
            }
            fprintf(stderr, "To dada buffer");
            to_dada_buffer();
            fprintf(stderr, "out dada buffer");
        }
    }
    // prev_ts = ts;

    return 0;
}

int spead_api_timer_callback(struct spead_api_module_shared *s, void *data)
{
    return 0;
}