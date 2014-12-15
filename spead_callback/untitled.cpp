unsigned int numWorkers;
unsigned int fIdSpeadId;
unsigned int timestampSpeadId;
unsigned int timestampsPerHeap;
unsigned int chanPerHeap;
unsigned int pols;
unsigned int bytesPerSample;
unsigned long long expectedHeapLen;

//DADA variables
unsigned int dadaBufId;
unsigned int dadaNumReaders;
unsigned int dadaNumBufs;
unsigned long long numHeapsPerBuf;
size_t dadaBufSize;

//Order buffer variables
unsigned long long obSize;
unsigned long long obSegment;
unsigned int obKey; 

//Dropped packet buffer size
unsigned int dpbSize;
unsigned int dpbKey;
unsigned int dbpSemKey;

#define NUM_HEAPS_PER_BUFFER 32 * 10 * NUM_WORKERS * 100

#define TIMESTAMPS_PER_HEAP 1
#define CHANS_PER_HEAP 1024
#define POLARIZATIONS 2
#define BYTES_PER_SAMPLE 1
#define EXPECTED_HEAP_LEN TS_PER_HEAP * CHANS_PER_HEAP * POLARIZATIONS * BYTES_PER_SAMPLE

#define DADA_BUFFER_SIZE EXPECTED_HEAP_LEN * NUM_HEAPS_PER_BUFFER
#define ORDER_BUFFER_SIZE NUM_WORKERS * EXPECTED_HEAP_LEN * 32
#define ORDER_BUFFER_SEGMENT NUM_WORKERS * EXPECTED_HEAP_LEN * 8 
#define DROPPED_PACKET_BUFFER_SIZE NUM_WORKERS * 4