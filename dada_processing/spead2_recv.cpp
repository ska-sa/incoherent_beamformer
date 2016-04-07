#include "dada_beamform.h"

// #include <stdio.h>
// #include <stdlib.h>
// #include <unistd.h>
// #include <errno.h>
// #include <string.h>
// #include <fcntl.h>
// #include <limits.h>
// #include <omp.h>
// #include <time.h>
// #include <signal.h>



//SPEAD send stuff
#include <iostream>
#include <utility>
#include <chrono>
#include <cstdint>
#include <boost/asio.hpp>
#include "spead2/common_defines.h"
#include "spead2/common_thread_pool.h"
#include "spead2/common_flavour.h"

#include "spead2/recv_udp.h"
#include "spead2/recv_heap.h"
#include "spead2/recv_live_heap.h"
#include "spead2/recv_ring_stream.h"
#include "spead2/recv_stream.h"

// #include "dada_hdu.h"
// #include "dada_def.h"
// #include "ascii_header.h"

// #include "spead2/common_flavour.h"
// #include "spead2/send_heap.h"
// #include "spead2/send_udp.h"
// #include "spead2/send_stream.h"

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define RESET "\033[0m"

#define DADA_BUF 0x1234

using boost::asio::ip::udp;

//Memory
uint64_t expected_heap_len = EXPECTED_HEAP_LEN;
uint64_t num_heaps_per_buffer = NUM_HEAPS_PER_BUFFER;
uint64_t num_heaps_per_order_buf = NUM_HEAPS_PER_ORDER_BUF;
uint64_t dada_buffer_size = expected_heap_len * num_heaps_per_buffer;
uint64_t order_buffer_size = expected_heap_len * num_heaps_per_order_buf;
uint64_t order_buffer_segment = order_buffer_size / 4;

unsigned char * order_buffer;
unsigned char * data;
char * dada_buffer;
dada_hdu_t * hdu;

int dada_id;

//Synchronisation variables
uint64_t first_ts;
uint64_t last_ts;
uint64_t pos;
uint64_t dada_head;


int synced = 0;

//Data locations in heap
int ts_pos = 2;
int data_pos = 0;

uint64_t n_complete = 0;


void connect_to_buffer (dada_hdu_t ** hdu, unsigned int dada_buf_key){
    fprintf(stderr, "yo\n");
    multilog_t* log;
    
    char* buffer;

    fprintf(stderr, "yo\n");

    log = multilog_open ("dada_simple_writer", 0);
    multilog_add (log, stderr);

    *hdu = dada_hdu_create(log);

    fprintf (stderr, KRED "dada_buf_id = %x\n" RESET, dada_buf_key);

    dada_hdu_set_key (*hdu, dada_buf_key);

    fprintf(stderr, "yo\n");


    if (*hdu == NULL){
        fprintf (stderr, "hdu null\n");
    }
    fprintf(stderr, "ho\n");

    if (dada_hdu_connect (*hdu) < 0 ){
        fprintf (stderr, KRED "Couldn't connect\n" RESET);
    }
    fprintf(stderr, "ho\n");

    if (dada_hdu_lock_write (*hdu) < 0){
        fprintf(stderr, KRED "CONNECT FAILED\n" RESET);
        // return EXIT_FAILURE;
    }

    fprintf(stderr, KGRN "yo\n" RESET);

    // uint64_t block_id;
    // buffer = ipcio_open_block_read (hdu->data_block, 2147483648,  &block_id);
}

void show_heap(const spead2::recv::heap &fheap)
{
    std::cout << "Received heap with CNT " << fheap.get_cnt() << '\n';
    const auto &items = fheap.get_items();
    std::cout << items.size() << " item(s)\n";
    for (const auto &item : items)
    {
        std::cout << "    ID: 0x" << std::hex << item.id << std::dec << ' ';
        std::cout << "[" << item.length << " bytes]";
        std::cout << '\n';
    }
    std::vector<spead2::descriptor> descriptors = fheap.get_descriptors();
    for (const auto &descriptor : descriptors)
    {
        std::cout
            << "    0x" << std::hex << descriptor.id << std::dec << ":\n"
            << "        NAME:  " << descriptor.name << "\n"
            << "        DESC:  " << descriptor.description << "\n";
        if (descriptor.numpy_header.empty())
        {
            std::cout << "        TYPE:  ";
            for (const auto &field : descriptor.format)
                std::cout << field.first << field.second << ",";
            std::cout << "\n";
            std::cout << "        SHAPE: ";
            for (const auto &size : descriptor.shape)
                if (size == -1)
                    std::cout << "?,";
                else
                    std::cout << size << ",";
            std::cout << "\n";
        }
        else
            std::cout << "        DTYPE: " << descriptor.numpy_header << "\n";
    }
}

void set_timestamp_header(dada_hdu_t * hdu, unsigned long long ts){
  // get the size of each header block
  uint64_t header_size = ipcbuf_get_bufsz (hdu->header_block);
  char buffer[64];

  // get a pointer to the header block
  char * header = ipcbuf_get_next_write (hdu->header_block);
  if (! header )
  {
    multilog (hdu->log, LOG_WARNING, "Could not get next header block\n");
    fprintf(stderr, KRED "Could not get next header block\n" RESET);
  }

  if (ascii_header_set (header, "TS", "%llu", ts) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write TELESCOPE to header\n");
    fprintf(stderr, KRED "Could not write TELESCOPE to header\n" RESET);
  }

  ipcbuf_mark_filled (hdu->header_block, header_size);
}
uint64_t lastpos;
void place_heap(spead2::recv::heap &heap)
{
    std::vector<spead2::recv::item> items;

    // show_heap(heap);

    
    items = heap.get_items();
    unsigned char* ts_buf = items[ts_pos].ptr;
    // TS is 5 byte unsigned 
    // uint64_t ts = (long long)ts_buf[0] + (long long)ts_buf[1] * 256 + (long long)ts_buf[2] * 256 * 256 + (long long)ts_buf[3] * 256 * 256 * 256 + (long long)ts_buf[4] * 256 * 256 * 256 * 256;

    uint64_t ts = 0;
    int i;
    for( i = 0; i < 5; i++ )
    {
        ts <<= 8;
        ts |= (uint64_t)ts_buf[i];
    }
    // paquet <<= 24;
    // printf("0x%" PRIx64 "\n", ts);

    // fprintf(stderr, "%02X%02X%02X%02X%02X\n", ts_buf[0], ts_buf[1], ts_buf[2], ts_buf[3], ts_buf[4]);
    // fprintf(stderr, "TS = %" PRIu64 "\n", ts);

    if(synced == 0){  // Sync to ACCUMULATION
        int ts_in = TIMESTAMP_INCREMENT;
        int acc = ACCUMULATE;
        fprintf(stdout, "TS = %" PRIu64 "\n diff = %" PRIu64 "\n", ts, ts - last_ts);
        
        if ( (ts / TIMESTAMP_INCREMENT) % ACCUMULATE == 0){
            fprintf(stdout, "SYNCED .. %" PRIu64 "\n", (ts / ts_in) % acc);
            first_ts = ts;
            last_ts = ts;
            synced = 1;
            set_timestamp_header(hdu, last_ts); //Set timestamp header
            uint64_t block_id;
            dada_buffer = ipcio_open_block_write (hdu->data_block, &block_id); //Open first buffer
        }
    }
    else //Synced
    {
        data = items[data_pos].ptr;
        pos = (ts - first_ts) / TIMESTAMP_INCREMENT * EXPECTED_HEAP_LEN;
        // memcpy (order_buffer + pos % order_buffer_size, data, EXPECTED_HEAP_LEN); //To order buffer
        // if (pos - lastpos != 8192)
        //     fprintf(stderr, KRED "diff = %" PRIu64 "\n Dropped %" PRIu64 "\n", pos - lastpos, (pos - lastpos) / 8192);

        if (pos % dada_buffer_size == 0) {//New dada buffer required
            last_ts = ts; 
            uint64_t block_id;
            fprintf (stderr, KGRN "NEW BUFFER\n");
            ipcio_close_block_write (hdu->data_block, dada_buffer_size); 
            set_timestamp_header(hdu, last_ts); //Set timestamp header
            // uint64_t block_id;
            dada_buffer = ipcio_open_block_write (hdu->data_block, &block_id);
        }

        memcpy(dada_buffer + pos % dada_buffer_size, data, EXPECTED_HEAP_LEN);
        lastpos = pos;
        // memset(order_buffer + dada_head % order_buffer_size, 0, order_buffer_segment);
        // dada_head += order_buffer_segment;
            
    }
        // memcpy(dada_buffer + dada_head % dada_buffer_size, order_buffer + dada_head % order_buffer_size, order_buffer_segment);
        

    
    // else //Synced
    // {
    //     data = items[data_pos].ptr;
    //     pos = (ts - first_ts) / TIMESTAMP_INCREMENT * EXPECTED_HEAP_LEN;
    //     memcpy (order_buffer + pos % order_buffer_size, data, EXPECTED_HEAP_LEN); //To order buffer
    //     if (pos - lastpos != 8192)
    //         fprintf(stderr, KRED "diff = %" PRIu64 "\n Dropped %" PRIu64 "\n", pos - lastpos, (pos - lastpos) / 8192);

    //     if (pos % dada_buffer_size == 0)
    //         last_ts = ts;

    //     if (pos - dada_head > order_buffer_segment*2){ // Order buffer filled, to dada

    //         if (dada_head % dada_buffer_size == 0){ //New dada buffer required
    //             uint64_t block_id;
    //             fprintf (stderr, KGRN "NEW BUFFER\n");
    //             ipcio_close_block_write (hdu->data_block, dada_buffer_size); 
    //             set_timestamp_header(hdu, last_ts); //Set timestamp header
    //             // uint64_t block_id;
    //             dada_buffer = ipcio_open_block_write (hdu->data_block, &block_id);
    //         }

    //         memcpy(dada_buffer + dada_head % dada_buffer_size, order_buffer + dada_head % order_buffer_size, order_buffer_segment);
    //         memset(order_buffer + dada_head % order_buffer_size, 0, order_buffer_segment);
    //         dada_head += order_buffer_segment;
            
    //     }
    //     lastpos = pos;

    // }
}


// Callback for spead stream
class icbf_stream : public spead2::recv::stream
{
private:

    virtual void heap_ready(spead2::recv::live_heap &&lheap) override
    {
        // std::cout << "Got heap " << lheap.get_cnt();
        if (lheap.is_complete())
        {
            // std::cout << " [complete]\n";
            n_complete++;
            spead2::recv::heap heap = spead2::recv::heap(std::move(lheap));
            place_heap(heap);
        }
        else if (lheap.is_contiguous())
            std::cout << " [contiguous]\n";
        else
            std::cout << " [incomplete]\n";
    }

    std::promise<void> stop_promise;

public:
    // using spead2::recv::stream::stream; (use base class constructor)

    explicit icbf_stream(spead2::thread_pool &pool, spead2::bug_compat_mask bug_compat = 0, std::size_t max_heaps = default_max_heaps) : stream(pool, bug_compat, max_heaps)
    {
        order_buffer = (unsigned char*)malloc(order_buffer_size); 
        prepare_dada();
    }

    virtual void stop_received() override
    {
        spead2::recv::stream::stop_received();
        stop_promise.set_value();
    }

    void join()
    {
        std::future<void> future = stop_promise.get_future();
        future.get();
    }

    //Connect to dada buffer
    void prepare_dada(){
        connect_to_buffer(&hdu, dada_id);      
    }
};

void run_callback(char* port){
    fprintf(stderr, "callback\n");
    spead2::thread_pool worker;
    icbf_stream stream(worker, spead2::BUG_COMPAT_PYSPEAD_0_5_2);
    boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::address_v4::any(), atoi(port));
    stream.emplace_reader<spead2::recv::udp_reader>(
        endpoint, spead2::recv::udp_reader::default_max_size, 1024 * 1024);
    stream.join();
}

int consume (dada_hdu_t * hdu1, char* port)
{

    fprintf(stderr, "ringbuffer\n");
    //Prepare Memory
    unsigned char * order_buffer = (unsigned char*)malloc(order_buffer_size); 
    unsigned char * data;
    char * dada_buffer;

    //Set up spead receiver
    spead2::thread_pool worker;

    spead2::recv::ring_stream<spead2::ringbuffer_semaphore<spead2::recv::live_heap> > stream(worker, 62);
    boost::asio::ip::udp::endpoint endpoint1(boost::asio::ip::address_v4::any(), atoi(port));

    stream.emplace_reader<spead2::recv::udp_reader>(
        endpoint1, spead2::recv::udp_reader::default_max_size, 512 * 1024 * 1024);


    int ts_pos = 1;
    int data_pos = 2;

    //Capture first spead heap
    std::vector<spead2::recv::item> items;

    spead2::recv::heap fh = stream.pop();

    //check heap
    show_heap(fh);
    
    items = fh.get_items();
    
    
    unsigned char* ts_buf = items[ts_pos].ptr;
    // TS is 5 byte unsigned 
    uint64_t ts = (uint64_t)ts_buf[0] + (uint64_t)ts_buf[1] * 256 + (uint64_t)ts_buf[2] * 256 * 256 + (uint64_t)ts_buf[3] * 256 * 256 * 256 + (uint64_t)ts_buf[4] * 256 * 256 * 256 * 256;

    uint64_t first_ts;
    uint64_t last_ts;
    uint64_t pos;
    uint64_t dada_head;

    //used to sync on a multiple of ACCUMULATION
    int synced = 0;

    //Capture Loop
    while(1){
        if(synced == 0){  // Sync to ACCUMULATION
            if (ts % ACCUMULATE == 0){
                first_ts = ts;
                last_ts = ts;
                synced = 1;
                set_timestamp_header(hdu, last_ts); //Set timestamp header
                uint64_t block_id;
                dada_buffer = ipcio_open_block_write (hdu->data_block, &block_id); //Open first buffer
            }
        }
        else //Synced
        {
            data = items[data_pos].ptr;
            pos = (ts - first_ts) / TIMESTAMP_INCREMENT * EXPECTED_HEAP_LEN;
            memcpy (order_buffer + pos % order_buffer_size, data, EXPECTED_HEAP_LEN); //To order buffer

            if (pos % dada_buffer_size == 0)
                last_ts = ts;

            if (pos - dada_head > order_buffer_segment){ // Order buffer filled, to dada

                if (dada_head % dada_buffer_size == 0){ //New dada buffer required
                    uint64_t block_id;
                    ipcio_close_block_write (hdu->data_block, dada_buffer_size); 
                    set_timestamp_header(hdu, last_ts); //Set timestamp header
                    dada_buffer = ipcio_open_block_write (hdu->data_block, &block_id);
                }

                memcpy(dada_buffer + dada_head % dada_buffer_size, order_buffer + dada_head % order_buffer_size, order_buffer_segment);
                memset(order_buffer + dada_head % order_buffer_size, 0, order_buffer_segment);
                dada_head += order_buffer_segment;
            }
        }

        spead2::recv::heap fh = stream.pop();

        //check heap
        show_heap(fh);
        
        items = fh.get_items();
        
        unsigned char* ts_buf = items[ts_pos].ptr;
        // TS is 5 byte unsigned 
        uint64_t ts = (uint64_t)ts_buf[0] + (uint64_t)ts_buf[1] * 256 + (uint64_t)ts_buf[2] * 256 * 256 + (uint64_t)ts_buf[3] * 256 * 256 * 256 + (uint64_t)ts_buf[4] * 256 * 256 * 256 * 256;
    }
}

int main (int argc, char **argv)
{
    // signal(SIGINT, INThandler);
    dada_hdu_t * hdu1;
    if (argv[1] == "rb"){
        connect_to_buffer(&hdu1, DADA_BUF);
        if (argc == 3)
            consume(hdu, argv[2]);
    }
    else
        if (argc == 4){
            dada_id = (int)strtol(argv[3], NULL, 0);
            run_callback(argv[2]);
        }
}