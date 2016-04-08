//SPEAD recieve stuff
#include <chrono>
#include <iostream>
#include <utility>
#include <chrono>
#include <cstdint>
#include <boost/asio.hpp>
#include "common_thread_pool.h"
#include "spead2/recv_udp.h"
#include "spead2/recv_heap.h"
#include "spead2/recv_live_heap.h"
#include "spead2/recv_ring_stream.h"

#include <unistd.h>

//DADA stuff
#include "dada_hdu.h"
#include "dada_def.h"
#include "ascii_header.h"

#include "dada_beamform.h"

#include <pthread.h>

// #include <thread>
// #include <mutex>
// #include <condition_variable>

#include <sys/shm.h>
#include "ipcutil.h"

#define DADA_BUF_1 0x1234
#define DADA_BUF_2 0x2345

#define ACCUMULATE 256
#define N_CHANS 1024
#define N_POLS 2
#define TIMESTAMPS_PER_HEAP 4
#define BYTES_PER_SAMPLE 1
#define TIMESTAMP_INCREMENT 2048

#define NUM_SYNC_LOOPS 2

#define ARTEMIS_IP "192.168.1.2"
#define FILE_LOC "/home/kat/data"

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define RESET "\033[0m"

typedef std::chrono::time_point<std::chrono::high_resolution_clock> time_point;

struct thread_data{
   int tid;
   int port;
   unsigned long long first_ts;
   int buffer_id;
   int ts_id;
   unsigned long long num_vals;
};

int master_id = 3;            //Master-thread which we synchronise to
unsigned long long tsync; //time to sync with
pthread_mutex_t tsync_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t master_id_mutex = PTHREAD_MUTEX_INITIALIZER;


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
    time_point now = std::chrono::high_resolution_clock::now();
    // std::chrono::duration<double> elapsed = now - start;
    // std::cout << elapsed.count() << "\n";
    // std::cout << std::flush;
}

void bf_align (uint16_t * beam, uint16_t * align, uint64_t num_vals, int64_t pos, uint64_t align_size){
    uint64_t beam_size = num_vals * sizeof(uint16_t);
    // fprintf (stderr, "pos = %llu, pos + beam_size = %llu, num_vals = %llu align_size = %llu\n", pos, pos+beam_size, num_vals, align_size);
    if (pos + beam_size < align_size){
        fprintf(stderr, "bf_align_1\n");
        beamform (beam, align + pos/2, align + pos/2, num_vals);
    }
    else{
        fprintf(stderr, "bf_align_2\n");
        uint64_t wrap = (align_size - pos) / sizeof(uint16_t);
        beamform (beam, align + pos/2, align + pos/2, wrap);
        fprintf(stderr, "bf_align_wrap\n");
        beamform (beam + wrap, align, align, num_vals - wrap);
    }
    // fprintf (stderr, "out_align\n");
}

//int port, unsigned long long first_ts, int buffer_id, int ts_id
void capture_spead(void* threadarg)
{
    fprintf (stderr, KYEL "IN THREAD\n" RESET);
    struct thread_data *my_data;
    my_data = (struct thread_data *) threadarg;
    int port = my_data->port;
    unsigned long long first_ts = my_data->first_ts;
    unsigned long long num_vals = my_data->num_vals;
    int buffer_id = my_data->buffer_id;
    int ts_id = my_data->ts_id;
    int tid = my_data->tid;

    uint16_t* order_buffer;
    unsigned long long ts2;
    long long tsdiff2;
    unsigned long long ts;
    uint64_t out_buffer_size = sizeof(uint16_t) * 67108864 * 10;

spead2::thread_pool worker;
    // std::shared_ptr<spead2::memory_pool> pool = std::make_shared<spead2::memory_pool>(16384, 26214400, 12, 8);
    spead2::recv::ring_stream<> stream1(worker, spead2::BUG_COMPAT_PYSPEAD_0_5_2, 64);
    //stream.set_memory_pool(pool);
    boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::address_v4::any(), port);
    stream1.emplace_reader<spead2::recv::udp_reader>(
        endpoint, spead2::recv::udp_reader::default_max_size, 8 * 1024 * 1024);
    fprintf (stderr, KYEL "ATTACHING\n" RESET);

    order_buffer = (uint16_t *)shmat(buffer_id, NULL, 0);
    // ts = *((unsigned long long *)shmat(ts_id, NULL, 0));

    fprintf (stderr, KYEL "ATTACHED\n" RESET);

    std::vector<spead2::recv::item> items1;

    spead2::recv::heap fh1 = stream1.pop();

    // show_heap(fh1);
    items1 = fh1.get_items();
    // prev2 = ts2;
    ts2 = *((unsigned long long *)items1[1].ptr);
    tsdiff2 = ts2 - first_ts;

    if (tsdiff2 > 0){
        int64_t pos2 = ((tsdiff2) * 67108864 / 536870912) % out_buffer_size / 2;
        bf_align ((uint16_t *) items1[1].ptr, order_buffer, num_vals, pos2, out_buffer_size / 2);
    }

    fprintf(stderr, "[%d] tsync = %llu\n", tid, tsync);

    for (int i = 0; i < NUM_SYNC_LOOPS; i++){
        if (ts2 < tsync && ts2 != 0){
            pthread_mutex_lock(&tsync_mutex);
            pthread_mutex_lock(&master_id_mutex);
            tsync = ts2;
            master_id = tid;
            pthread_mutex_unlock(&tsync_mutex);
            pthread_mutex_unlock(&master_id_mutex);
            fprintf(stderr, "[%d] new tsync = %llu\n", tid, tsync);
        }
        fh1 = stream1.pop();
        items1 = fh1.get_items();
        // prev2 = ts2;
        ts2 = *((unsigned long long *)items1[1].ptr);

        fprintf (stderr, "[%d] ts2 = %llu\n", tid, ts2);
        fprintf (stderr, "[%d] *((unsigned long long *)items1[0].ptr = %llu\n", tid, *((unsigned long long *)items1[1].ptr));
        tsdiff2 = ts2 - first_ts;
    }

    if (master_id == tid){
         pthread_mutex_lock(&tsync_mutex);
         tsync = ts2;
         pthread_mutex_unlock(&tsync_mutex);
    }

    while (true)
    {
        try{
            
            pthread_mutex_lock(&tsync_mutex);
            ts = tsync;
            pthread_mutex_unlock(&tsync_mutex);

            int64_t diff = ts - ts2;

            // fprintf (stderr, "[%d] ts = %llu\n", tid, ts);
            // fprintf (stderr, "[%d] ts2 = %llu\n", tid, ts2);
            // fprintf (stderr, "[%d] diff = %lld\n", tid, diff);

            if (diff > 0 || master_id == tid){
                fh1 = stream1.pop();
                items1 = fh1.get_items();
                // prev2 = ts2;
                ts2 = *((unsigned long long *)items1[1].ptr);
                tsdiff2 = ts2 - first_ts;
                diff = ts - ts2;
            }
            else
                sleep(1);

            // if (diff > 0 || ts == 0){
                fprintf (stderr, KYEL "[%d] second\n" RESET, tid, (diff)/536870912);
                int64_t pos2 = ((tsdiff2) * 67108864 / 536870912) % out_buffer_size/2;
                bf_align ((uint16_t *) items1[1].ptr, order_buffer, num_vals, pos2, out_buffer_size/2);
                // sync[1][pos2/67108864/sizeof(uint16_t)] = 1;
                fh1 = stream1.pop();
                items1 = fh1.get_items();
                // prev2 = ts2;
                ts2 = *((unsigned long long *)items1[1].ptr);
                tsdiff2 = ts2 - first_ts;

            // }
            // else{
            //     if ((diff)/536870912 < -1)
            //         fprintf(stderr, "[%d] - ts = %llu, ts2 = %llu\n", tid, ts, ts2);
            // }

            // fprintf (stderr, KRED "[%d] diff2 : %lld\n" RESET, tid, (tsdiff2)/536870912);
            fprintf (stderr, KRED "[%d] diff : %lld\n" RESET, tid, (diff)/536870912);

            if (master_id == tid){
                pthread_mutex_lock(&tsync_mutex);
                tsync = ts2;
                pthread_mutex_unlock(&tsync_mutex);
            }
        }
        catch (spead2::ringbuffer_stopped &e)
        {
            break;
        }
    }

}

void run (int port1, int port2, int port3, dada_hdu_t * hdu)
{
    pthread_t threads[3];
    struct thread_data thread_data_array[3];

    unsigned long long first_ts, ts2;
    // unsigned long long * ts;
    uint64_t heap_size = N_POLS * N_CHANS * BYTES_PER_SAMPLE * TIMESTAMPS_PER_HEAP;

    int out_file = 0;
    char filename[255];
    uint64_t count = 0;
    unsigned long long read_head = 0;

    uint64_t out_buffer_size = sizeof(uint16_t) * 67108864 * 10;
    fprintf (stderr, "out_buffer_size = %llu\n", out_buffer_size);
    int ob_id, ts_id;
    uint16_t * out;
    out = ipc_alloc("6543", out_buffer_size, IPC_CREAT | IPC_EXCL | 0666, &ob_id);
    // ts = ipc_alloc("5432", sizeof(unsigned long long), IPC_CREAT | IPC_EXCL | 0666, &ts_id);
    memset(out,0,out_buffer_size);

    if (dada_hdu_lock_read (hdu) < 0){
         fprintf(stderr, KRED "hdu CONNECT FAILED\n" RESET);
         // return EXIT_FAILURE;
    }

    initial_header(hdu);
    
    if (ipcio_is_open (hdu->data_block)){
        fprintf (stderr, KGRN "OPEN\n" RESET);
    }

    // prev1 = ts1;
    first_ts = get_timestamp(hdu);
    if (first_ts == 0){
        fprintf (stderr, "first_ts = 0\n");
        first_ts = get_timestamp(hdu);
    }

    ts2 = first_ts;
    pthread_mutex_lock(&tsync_mutex);
    tsync = first_ts; //Initial sync ts
    pthread_mutex_unlock(&tsync_mutex); 
    fprintf(stderr,"[3] tsync = %llu\n", tsync);
    unsigned long long num_vals, pos1, blockid;
    char * buffer;

    buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);

    uint16_t * accumulated = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);
    

    num_vals = accumulate (hdu->data_block->curbuf, accumulated, hdu->data_block->curbufsz);
    bf_align (accumulated, out, num_vals, pos1, out_buffer_size / 2);
    // sync[0][pos1/67108864/sizeof(uint16_t)] = 1;
    
    // ssize_t size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);
    ssize_t size;
    free (accumulated);

    thread_data_array[0].tid = 0;
    thread_data_array[0].port = 7161;
    thread_data_array[0].first_ts = first_ts;
    thread_data_array[0].buffer_id = ob_id;
    thread_data_array[0].ts_id = ts_id;
    thread_data_array[0].num_vals = num_vals;
    fprintf (stderr, KGRN "CREATE THREAD\n" RESET);

    thread_data_array[1].tid = 1;
    thread_data_array[1].port = 7162;
    thread_data_array[1].first_ts = first_ts;
    thread_data_array[1].buffer_id = ob_id;
    thread_data_array[1].ts_id = ts_id;
    thread_data_array[1].num_vals = num_vals;

    thread_data_array[2].tid = 2;
    thread_data_array[2].port = 7163;
    thread_data_array[2].first_ts = first_ts;
    thread_data_array[2].buffer_id = ob_id;
    thread_data_array[2].ts_id = ts_id;
    thread_data_array[2].num_vals = num_vals;

    pthread_create(&threads[0], NULL, capture_spead, &thread_data_array[0]);
    pthread_create(&threads[1], NULL, capture_spead, &thread_data_array[1]);
    pthread_create(&threads[2], NULL, capture_spead, &thread_data_array[2]);

    int64_t tsdiff2;

    for (int i = 0; i < NUM_SYNC_LOOPS; i++){
        if (ts2 < tsync){
            pthread_mutex_lock(&tsync_mutex);
            pthread_mutex_lock(&master_id_mutex);
            tsync = ts2;
            master_id = 3;
            pthread_mutex_unlock(&tsync_mutex);
            pthread_mutex_unlock(&master_id_mutex);
            fprintf(stderr, "[%d] new tsync = %llu\n", 3, tsync);
        }
        size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);

        // free(accumulated);
        // prev2 = ts2;
        ts2 = get_timestamp(hdu);
        tsdiff2 = ts2 - first_ts;

        buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);
    }

    snprintf(filename,255,"%s/%llu.dat",FILE_LOC, first_ts);

    out_file = open(filename, O_WRONLY | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    fprintf (stderr, "opened file %s", filename);


    while (true)
    {

        pthread_mutex_lock(&tsync_mutex);
        unsigned long long ts = tsync;
        pthread_mutex_unlock(&tsync_mutex);

        fprintf (stderr, KGRN "GRABBED\n" RESET);
        int64_t diff = ts - ts2;
        accumulated = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);

        if (diff > 0 || master_id == 3){
            fprintf(stderr, "diff = %lld\n", diff);
            
            int64_t pos1 = ((tsdiff2) * 67108864 / 536870912) % out_buffer_size/2;;


            num_vals = accumulate (hdu->data_block->curbuf, accumulated, hdu->data_block->curbufsz);
            bf_align (accumulated, out, num_vals, pos1, out_buffer_size / 2);
            // sync[0][pos1/67108864/sizeof(uint16_t)] = 1;
            size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);

            // free(accumulated);

            ts2 = get_timestamp(hdu);
            tsdiff2 = ts2 - first_ts;

            buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);

            diff = ts - ts2;
            count++;
        }
        else
            sleep(1);

        if (diff > 0){
            fprintf(stderr, "diff2 = %lld\n", diff);

            accumulated = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);
            int64_t pos1 = ((tsdiff2) * 67108864 / 536870912) % out_buffer_size/2;;

            num_vals = accumulate (hdu->data_block->curbuf, accumulated, hdu->data_block->curbufsz);
            bf_align (accumulated, out, num_vals, pos1, out_buffer_size / 2);
            // sync[0][pos1/67108864/sizeof(uint16_t)] = 1;
            size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);

            ts2 = get_timestamp(hdu);
            tsdiff2 = ts2 - first_ts;


            buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);
            count++;

            
        }

        fprintf (stderr, KRED "[%d] diff : %lld\n" RESET, 3, (diff)/536870912);

        if (master_id == 3){
            pthread_mutex_lock(&tsync_mutex);
            tsync = ts2;
            pthread_mutex_unlock(&tsync_mutex);
        }

        if (tsdiff2 * 67108864 / 536870912 - read_head > num_vals * sizeof(uint16_t) * 2)
        {
            fprintf (stderr, "read_head = %llu\n", read_head);
            // fprintf (stderr, "tsdiff2 = %lld\n", tsdiff2);
            // fprintf (stderr, "(tsdiff2 * 67108864 / 536870912 - read_head) = %lld, num_vals * sizeof(uint16_t) * 2 = %d\n", (tsdiff2 * 67108864 / 536870912 - read_head), num_vals * sizeof(uint16_t) * 2);
            int64_t size = (tsdiff2 * 67108864 / 536870912 - read_head) - (num_vals * sizeof(uint16_t) * 2);
            // pwrite(out_file, out + read_head % out_buffer_size, size, read_head);
            pwrite(out_file, accumulated, sizeof(uint16_t) * 67108864, read_head);
            // pwrite(out_file, buffer, sizeof(uint16_t) * 67108864, read_head);
            memset(out + read_head % out_buffer_size, 0, size);
            read_head = read_head + sizeof(uint16_t) * 67108864;
            // read_head = read_head + size;
            // read_head = read_head + sizeof(uint16_t) * 67108864;
        }

        free(accumulated);


    }
}


int main (int argc, char **argv)
{
    dada_hdu_t * hdu;
    connect_to_buffer(&hdu, DADA_BUF_1);
    run (7160, 7161, 7162, hdu);
}
