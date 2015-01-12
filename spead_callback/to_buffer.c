#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>

//For Dada thread

#include <signal.h>
#include <pthread.h>


// #include "to_buffer.h"

#include "dada_hdu.h"
#include "dada_def.h"
#include "ascii_header.h"

#include "ipcutil.h"
#include <sys/shm.h>
#include <sys/sem.h>

#include <signal.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <spead_api.h>

#include <time.h>

#include <sys/time.h>
#include <assert.h>

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

#define ORDER_BUFFER_KEY 0x10236
#define DROPPED_PACKET_BUFFER_KEY 0x10237
#define DROPPED_PACKET_BUFFER_SEM_KEY 0x10238

#define ORDER_BUFFER_TAIL_SEM_KEY 0x10240

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

//Dropped packet buffer
unsigned int dpbSize;
unsigned int dpbKey;
unsigned int dbpSemKey;
unsigned int o_b_sem_key;
unsigned int numBitsInSegment;
unsigned int noDroppedPackets;
unsigned int numDroppedPackets;
unsigned int numRecievedPackets;

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

    fprintf(stderr, "numheapsperbuff = %zu\n", numHeapsPerBuf);

    dadaBufSize = expectedHeapLen * numHeapsPerBuf;

    fprintf(stderr, "dadaBufSize = %zu\n", dadaBufSize);

    obSize = numHeapsPerOB * expectedHeapLen;

    if (numHeapsPerOB % 4 != 0){
        fprintf(stderr, "number of heaps in order buffer = %u, should be divisible by 4");
        exit(0);
    }

    obSegment = obSize / 4;
    numBitsInSegment = numHeapsPerOB / 4;
    noDroppedPackets = (1 << numBitsInSegment) - 1;

    fprintf(stderr, "noDroppedPackets = %u", noDroppedPackets);

    env_temp = getenv("ORDER_BUFFER_KEY");
    if (env_temp != NULL) obKey = atoi(env_temp);
    else obKey = ORDER_BUFFER_KEY;

    dpbSize = numHeapsPerOB * 32 / numBitsInSegment;  //so that each dpbbuffer location has 1 segment

    env_temp = getenv("DROPPED_PACKET_BUFFER_KEY");
    if (env_temp != NULL) dpbKey = atoi(env_temp);
    else dpbKey = DROPPED_PACKET_BUFFER_KEY;

    env_temp = getenv("DROPPED_PACKET_BUFFER_SEM_KEY");
    if (env_temp != NULL) dbpSemKey = atoi(env_temp);
    else dbpSemKey = DROPPED_PACKET_BUFFER_SEM_KEY;

    env_temp = getenv("ORDER_BUFFER_TAIL_SEM_KEY");
    if (env_temp != NULL) o_b_sem_key = atoi(env_temp);
    else o_b_sem_key = ORDER_BUFFER_TAIL_SEM_KEY;

}

typedef enum { false, true } bool;

char* buffer; //local pointer to current buffer
unsigned long long prior_ts; //local prior ts

char* order_buffer;                     //buffer to reorder/zero heaps
int* dropped_packet_buffer;             //bit array to store whether a heap has been dropped
unsigned long long order_buffer_tail;   //Keeps track of the total number of heaps
int o_b_sem_id;                         //order buffer sempahore key
struct sembuf sb;                       //oreder buffer semaphore

int o_b_tail_sem_id;
struct sembuf o_b_tail_sem;                 //semaphore for the order buffer tail variable.

clock_t start;
uint64_t diff;
uint64_t num_bytes_transferred;

multilog_t* log;
dada_hdu_t * hdu;

pthread_t dada;
sigset_t sset;
int sig, n1 = 1;

int ob_id;                      //SHMEM key for order buffer
int dpb_id; 

struct snap_shot
{
    //from simple writer

    unsigned int numHeaps;
    int parentPID;

    unsigned long long order_buffer_tail;
    

    int master_pid;
    char* buffer; //global pointer to current buffer (NOT NECESSARY?)
    unsigned long long prior_ts; //first ts for this buffer
    int buffer_created;  //Is the buffer full (NOTNECESSARY?)
    int buffer_connected;
    int first_thread;
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

    fprintf(stderr, ": PID [%d] master PID = \n",getpid());
    struct snap_shot *ss;
    ss = get_data_spead_api_module_shared(s);
    if ((ss != NULL) && (getpid() == ss->master_pid)){

        fprintf(stderr, KGRN "numHeaps = %u\n"RESET, ss->numHeaps);
        fprintf(stderr, KGRN "num recieved heaps = %u\n"RESET, numRecievedPackets);
        fprintf(stderr, KGRN "num dropped heaps = %u\n"RESET, numDroppedPackets);
        fprintf(stderr, KGRN "Dropped %u%%\n"RESET, numDroppedPackets * 100 /numRecievedPackets);

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

// function to write the header to the datablock
int simple_writer_open (dada_hdu_t * hdu)
{
  // get the size of each header block
  uint64_t header_size = ipcbuf_get_bufsz (hdu->header_block);

  // get a pointer to the header block
  char * header = ipcbuf_get_next_write (hdu->header_block);
  if (! header )
  {
    multilog (hdu->log, LOG_WARNING, "Could not get next header block\n");
    fprintf(stderr, KRED "Could not get next header block\n" RESET);
  }

  // now write all the required key/value pairs to the header. Some 
  // examples shown below
  char buffer[64];
  sprintf (buffer, "%02d:%02d:%02d.%d", 4, 37, 15, 883250);
  if (ascii_header_set (header, "RA", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write RA to header\n");
    fprintf(stderr, KRED "Could not write RA to header\n" RESET);
  }

  sprintf (buffer, "%02d:%02d:%02d.%d", -47, 15, 9, 31863);
  if (ascii_header_set (header, "DEC", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write DEC to header\n");
    fprintf(stderr, KRED "Could not write DEC to header\n" RESET);
  }

  float tsamp = 0.000256*1000000;
  if (ascii_header_set (header, "TSAMP", "%f", tsamp) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write TSAMP to header\n");
    fprintf(stderr, KRED "Could not write TSAMP to header\n" RESET);
  }

  fprintf (stderr, KRED "HEADER BLOCK SIZE = %llu"RESET, header_size);
  fprintf (stderr, KRED "HEADER BLOCK SIZE = %llx"RESET, header_size);

  sprintf (buffer, "KAT7");
  if (ascii_header_set (header, "TELESCOPE", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write TELESCOPE to header\n");
    fprintf(stderr, KRED "Could not write TELESCOPE to header\n" RESET);
  } 

  sprintf (buffer, "KPSR");
  if (ascii_header_set (header, "INSTRUMENT", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write TELESCOPE to header\n");
    fprintf(stderr, KRED "Could not write TELESCOPE to header\n" RESET);
  } 

  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "SOURCE", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }
  
  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "CALFREQ", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }

  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "FREQ", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }

  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "BANDWIDTH", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }

  
  if (ascii_header_set (header, "NPOL", "%d", 1) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }

  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "NBIT", "%d", 8) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }

  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "NCHAN", "%d", 1024) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }

  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "NDIM", "%d", 2) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }

  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "BANDWIDTH", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }

  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "OBS_OFFSET", "%d", 0) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }

  sprintf (buffer, "PLACEHOLDER");
  if (ascii_header_set (header, "RESOLUTION", "%d", 1) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write SOURCE to header\n");
    fprintf(stderr, KRED "Could not write SOURCE to header\n" RESET);
  }


  // DSPSR requires:
  // TELESCOPE  : Telescope name e.g. Parkes
  // SOURCE     : source name e.g. J0437-4715
  // CALFREQ    : only used for CAL observations e.g. 11.125 [MHz]
  // FREQ       : centre frequnecy of entire band e.g. 1100 [MHz]
  // BW         : bandwidth of entire band e.g. 200 MHz
  // NPOL       : number of polarisations e.g. 2
  // NBIT       : number of bits per sample .e.g. 8
  // NCHAN      : number of channels e.g. 512
  // NDIM       : number of dimensions .e.g 2 for complex valued input
  // TSAMP      : sampling time in micro seconds
  // UTC_START  : UTC time of first sample in YYYY-MM-DD-HH:MM:SS format
  // OBS_OFFSET : Offset in bytes from above time

  // after all required header parameters filled in, marked the header as filled (valid)
  ipcbuf_mark_filled (hdu->header_block, header_size);

  return 0;
}

int count;

void * dadaThread(void *x)
{
    count = 0;
    int sig;

    log = multilog_open ("dada_simple_writer", 0);
    multilog_add (log, stderr);
    // create PSRDADA header + dada struct
    hdu = dada_hdu_create (log);
    // set the key (this should match the key used to create the SMRB with dada_db -k XXXX)
    dada_hdu_set_key(hdu, dadaBufId);

    if (hdu == NULL)
    {
        fprintf (stderr, "hdu null\n");
    }

    if (dada_hdu_connect (hdu) < 0)
            fprintf(stderr, KRED "couldn't connect\n" RESET);

    if (dada_hdu_lock_write (hdu) < 0){
        fprintf(stderr, KRED "CONNECT FAILED" RESET);
        // return EXIT_FAILURE;
    }
    simple_writer_open(hdu);

    if (hdu == NULL)
    {
        fprintf (stderr, "hdu null\n");
    }
    if (hdu->data_block == NULL)
    {
        fprintf (stderr, "data_block null\n");
    }

    uint64_t block_id;
    buffer = ipcio_open_block_write (hdu->data_block, &block_id);

    if (!buffer)
    {
      multilog (hdu->log, LOG_ERR, "write: ipcio_open_block_write failed\n");
      fprintf(stderr, KRED "NO BUFFER\n");
      return -1;
    }

    while(1){
        sigwait(&sset, &sig);

        if (sig == SIGUSR1){
            to_dada_buffer();
        }

        if (sig == SIGUSR2){
            ipcio_close_block_write (hdu->data_block, dadaBufSize);
            uint64_t block_id;
            buffer = ipcio_open_block_write (hdu->data_block, &block_id);

            if (!buffer)
            {
              multilog (hdu->log, LOG_ERR, "write: ipcio_open_block_write failed\n");
              fprintf(stderr, KRED "NO BUFFER\n" RESET);
              return -1;
            }

        }

        if (sig == SIGINT){
            fprintf(stderr, KGRN "EXITING DADA THREAD"RESET);
            if (dada_hdu_unlock_write (hdu) < 0)
                return EXIT_FAILURE;

            // disconnect from the SMRB
            if (dada_hdu_disconnect (hdu) < 0)
                return EXIT_FAILURE;

            return EXIT_SUCCESS;
        }
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
    numDroppedPackets = 0;
    numRecievedPackets = 0;

    sigemptyset(&sset);
    sigaddset(&sset, SIGUSR1);
    sigaddset(&sset, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &sset, NULL);

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
        ss->prior_ts = 0;
        ss->first_thread = 0;
        ss->numHeaps = 0;

        if (ss->buffer_created == 0){
            ss->buffer_created == 1;

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
            fprintf (stderr, KYEL "CREATING dada thread\n" RESET);
            ob_id = ss-> ob_id;
            dpb_id = ss->dpb_id;
            fprintf (stderr, KRED "ob_id = %d\n" RESET, ss->ob_id);
            fprintf (stderr, KRED "dpb_id = %d\n" RESET, ss->dpb_id);
            pthread_create(&dada, NULL, dadaThread, &n1);
            ss->parentPID = getpid();
        }

        set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
    }

    num_bytes_transferred = 0;
    unlock_spead_api_module_shared(s);
}

void set_bit(int* array, int pos)  //Set the bit in the DPB associated with this heap
{
    sb.sem_op = -1;  
    if (semop(o_b_sem_id, &sb, 1) == -1) {
        perror("semop");
        fprintf(stderr, KRED "SEMOP CLOSE FAIL" RESET);
        exit(1);
    }

    array[pos/numBitsInSegment] |= 1 << (pos % numBitsInSegment);
    sb.sem_op = 1;

    if (semop(o_b_sem_id, &sb, 1) == -1) {
        perror("semop");
        fprintf(stderr, KRED "SEMOP OPEN FAIL" RESET);
        exit(1);
    }

}

int get_bit(int* array, int pos)
{
    return ((array[pos/numBitsInSegment] & (1 << (pos%numBitsInSegment) )) != 0);
}

void write_to_order_buffer(char* heap, unsigned long long offset)
{
    unsigned long long o_b_off = offset %  obSize;
    memcpy(order_buffer + o_b_off, heap, expectedHeapLen);
    int dp_pos = o_b_off/expectedHeapLen; //This is the bit location of this packet in the dpb (essentially it is a (heap#)%(#heaps in order buffer) assuming the first heap# is 0)
    set_bit(dropped_packet_buffer, dp_pos);
}

void zero_dropped_packets()  //Should only be called by master thread
{
    int o_b_off = order_buffer_tail %  obSize;
    int i = 0;
    for (i = 0; i < numBitsInSegment; i++){
        if (get_bit(dropped_packet_buffer, o_b_off / expectedHeapLen + i))
        {
            memset(buffer + o_b_off, 0, obSegment);
            numDroppedPackets +=1;
        }
    }
}



void to_dada_buffer ()  //Should only be called by master thread
{  
    if (buffer == NULL)
    {
        fprintf(stderr, "NO BUFFER\n");
    }
    if (order_buffer == NULL)
        fprintf(stderr, "NO ORDER BUFFER\n");

    int o_b_off = order_buffer_tail %  obSize;
    memcpy(buffer + order_buffer_tail, order_buffer + o_b_off, obSegment);
    order_buffer_tail = (order_buffer_tail + obSegment) % dadaBufSize;

    if (dropped_packet_buffer[o_b_off / expectedHeapLen / 32] != noDroppedPackets)
        zero_dropped_packets();

    numRecievedPackets += numBitsInSegment;
    dropped_packet_buffer[o_b_off / expectedHeapLen / numBitsInSegment] = 0;
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

    if (ss == NULL)
    {
        fprintf(stderr, KRED "ss is null\n" RESET);
        unlock_spead_api_module_shared(s);
        //Shared resources empty, unlock and return
        return -1;
    }

    lock_spead_api_module_shared(s);

    if (ss->first_thread == 0) { //First thread to run

        start = clock(), diff;
        ss->first_thread = 1;
        ss->master_pid = getpid();
        fprintf(stderr, KGRN "MASTER thread is %d\n" RESET, getpid());
        ss->prior_ts = ts;
        ss->buffer_connected = 1;
    }

    ss->numHeaps+=1;
    set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
    unlock_spead_api_module_shared(s);
    
    if (ss->buffer_connected == 1){
        if (order_buffer == NULL){ //Must be first time non master thread has run
            prior_ts = ss->prior_ts;
            order_buffer = (char *)shmat(ss->ob_id, NULL, 0);
            dropped_packet_buffer = (char *)shmat(ss->dpb_id, NULL, 0);
            sb = ss->sb;
            o_b_sem_id = ss->o_b_sem_id;
            order_buffer_tail = 0;
        }

        if (prior_ts < ss->prior_ts)
            prior_ts = ss->prior_ts;

        offset = (ts - prior_ts) / timestampIncrement * expectedHeapLen;
        write_to_order_buffer(itm, offset);  //Copy data to buffer
        lock_spead_api_module_shared(s);
        long long check = offset - ss->order_buffer_tail; //If this difference is greater than a orderbuffer segment, then we should move data to dada buffer

        if (check < 0)
            check = check * -1;

        if (( check > obSegment * 2) )
        {
            if (offset >= dadaBufSize){  //If the offset is greater than a DADA buffer, we need to open a new buffer and reset counters
                int ret = kill(ss->parentPID,SIGUSR2);
                unsigned long long add = numHeapsPerBuf * timestampIncrement; //How much to add to the timstamp
                ss->prior_ts = (ss->prior_ts + add) % ULLONG_MAX;  //New prior_ts
                prior_ts = ss->prior_ts;
                offset = (ts - prior_ts) / timestampIncrement * expectedHeapLen;
            }

            int ret = kill(ss->parentPID,SIGUSR1);
            ss->order_buffer_tail = (ss->order_buffer_tail + obSegment) % dadaBufSize;
            set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
        }
    }
    else
        fprintf(stderr, KYEL "NOT CONNECTED YET\n" RESET);
    unlock_spead_api_module_shared(s);

    return 0;
}

int spead_api_timer_callback(struct spead_api_module_shared *s, void *data)
{
    return 0;
}