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

//DADA stuff
#include "dada_hdu.h"
#include "dada_def.h"
#include "ascii_header.h"

#include "dada_beamform.h"

#define DADA_BUF_1 0x1234
#define DADA_BUF_2 0x2345

#define ACCUMULATE 256
#define N_CHANS 1024
#define N_POLS 2
#define TIMESTAMPS_PER_HEAP 4
#define BYTES_PER_SAMPLE 1
#define TIMESTAMP_INCREMENT 2048

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

void beamform_4 (u_int16_t * acc1, u_int16_t * acc2, u_int16_t * acc3, u_int16_t * acc4, u_int16_t * beamformed, uint64_t num_vals){
    int i;
    // beamformed = (uint16_t*)malloc(num_vals * sizeof(uint16_t));
    for (i = 0; i < num_vals; i++)
    {
        beamformed[i] = acc1[i] + acc2[i] + acc3[i] + acc4[i];
    }
}


static void run_ringbuffered(int port1, int port2, int port3, dada_hdu_t * hdu)
{
    spead2::thread_pool worker;
    // std::shared_ptr<spead2::memory_pool> pool = std::make_shared<spead2::memory_pool>(16384, 26214400, 12, 8);

    spead2::recv::ring_stream<spead2::ringbuffer_semaphore<spead2::recv::live_heap> > stream1(worker, 128);
    spead2::recv::ring_stream<spead2::ringbuffer_semaphore<spead2::recv::live_heap> > stream2(worker, 128);
    spead2::recv::ring_stream<spead2::ringbuffer_semaphore<spead2::recv::live_heap> > stream3(worker, 128);

    // stream1.set_memory_pool(pool);
    // stream2.set_memory_pool(pool);
    // stream3.set_memory_pool(pool);

    boost::asio::ip::udp::endpoint endpoint1(boost::asio::ip::address_v4::any(), 7161);
    boost::asio::ip::udp::endpoint endpoint2(boost::asio::ip::address_v4::any(), 7162);
    boost::asio::ip::udp::endpoint endpoint3(boost::asio::ip::address_v4::any(), 7163);

    stream1.emplace_reader<spead2::recv::udp_reader>(
        endpoint1, spead2::recv::udp_reader::default_max_size, 128 * 1024 * 1024);
    stream2.emplace_reader<spead2::recv::udp_reader>(
        endpoint2, spead2::recv::udp_reader::default_max_size, 128 * 1024 * 1024);
    stream3.emplace_reader<spead2::recv::udp_reader>(
        endpoint3, spead2::recv::udp_reader::default_max_size, 128 * 1024 * 1024);

    unsigned long long ts1, ts2, ts3, ts4;
    long long tsdiff2 = 1, tsdiff3 = 1, tsdiff4 = 1, prev1, prev2, prev3, prev4;

    uint64_t heap_size = N_POLS * N_CHANS * BYTES_PER_SAMPLE * TIMESTAMPS_PER_HEAP;
    uint64_t buffer_allignment = 13; //Never going to be 13, could be 0

        if (dada_hdu_lock_read (hdu) < 0){
         fprintf(stderr, KRED "hdu CONNECT FAILED\n" RESET);
         // return EXIT_FAILURE;
    }

    initial_header(hdu);
    
    if (ipcio_is_open (hdu->data_block)){
        fprintf (stderr, KGRN "OPEN\n" RESET);
    }

    uint16_t * align1, * align2, * align3;
    align1 = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);
    align2 = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);
    align3 = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);
    memset(align1,0,67108864 * sizeof(uint16_t));
    memset(align1,0,67108864 * sizeof(uint16_t));
    memset(align1,0,67108864 * sizeof(uint16_t));
    std::vector<spead2::recv::item> items1, items2, items3;

    while (true)
    {
        try
        {
            // if (buffer_allignment == 13){
            prev1 = ts1;
            ts1 = get_timestamp(hdu);
            // }
            uint16_t * accumulated, * beamformed;
            uint64_t blockid, num_vals;
            char * buffer;

            buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);

            accumulated = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);
            beamformed = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);
            


            num_vals = accumulate (hdu->data_block->curbuf, accumulated, hdu->data_block->curbufsz);

            // fprintf(stderr, "ts1 = %llu\n", ts1);
            
            
            
            // n_complete++;
            // show_heap(fh);
            // const auto items1 = new item;
            // const auto items2 = new item;
            // const auto items3 = new item;
            fprintf (stderr, KGRN "ts1 = %llu\n" RESET, ts1);

            spead2::recv::heap fh1 = stream1.pop();
            items1 = fh1.get_items();
            prev2 = ts2;
            ts2 = *((unsigned long long *)items1[0].ptr);
            tsdiff2 = ts2 < ts1? ts1 - ts2 : - static_cast< long long >( ts2 - ts1 );
            fprintf (stderr, KGRN "ts2 = %llu\n" RESET, ts2);
            fprintf (stderr, KGRN "tsdiff2 = %lld\n" RESET, tsdiff2);

            if (tsdiff2 > 0){
                fh1 = stream1.pop();
                items1 = fh1.get_items();
                prev2 = ts2;
                ts2 = *((unsigned long long *)items1[0].ptr);
                tsdiff2 = ts2 < ts1? ts1 - ts2 : - static_cast< long long >( ts2 - ts1 );
                fprintf (stderr, KGRN "ts2 = %llu\n" RESET, ts2);
                fprintf (stderr, KGRN "tsdiff2 = %lld\n" RESET, tsdiff2);
            }

            spead2::recv::heap fh2 = stream2.pop();
            items2 = fh2.get_items();
            prev3 = ts3;
            ts3 = *((unsigned long long *)items2[0].ptr);
            tsdiff3 = ts3 < ts1? ts1 - ts3 : - static_cast< long long >( ts3 - ts1 );
            fprintf (stderr, KGRN "ts3 = %llu\n" RESET, ts3);
            fprintf (stderr, KGRN "tsdiff3 = %lld\n" RESET, tsdiff3);
            

            if (tsdiff3 > 0){
                fh2 = stream2.pop();
                items2 = fh2.get_items();
                prev3 = ts3;
                ts3 = *((unsigned long long *)items2[0].ptr);
                tsdiff3 = ts3 < ts1? ts1 - ts3 : - static_cast< long long >( ts3 - ts1 );
                fprintf (stderr, KGRN "ts3 = %llu\n" RESET, ts3);
                fprintf (stderr, KGRN "tsdiff3 = %lld\n" RESET, tsdiff3);
            }

            spead2::recv::heap fh3 = stream3.pop();
            items3 = fh3.get_items();
            prev4 = ts4;
            ts4 = *((unsigned long long *)items3[0].ptr);
            tsdiff4 = ts4 < ts1? ts1 - ts4 : - static_cast< long long >( ts4 - ts1 );
            fprintf (stderr, KGRN "ts4 = %llu\n" RESET, ts4);
            fprintf (stderr, KGRN "tsdiff4 = %lld\n" RESET, tsdiff4);

            if (tsdiff4 > 0){
                fh3 = stream3.pop();
                items3 = fh3.get_items();
                prev4 = ts4;
                ts4 = *((unsigned long long *)items3[0].ptr);
                tsdiff4 = ts4 < ts1? ts1 - ts4 : - static_cast< long long >( ts4 - ts1 );
                fprintf (stderr, KGRN "ts4 = %llu\n" RESET, ts4);
                fprintf (stderr, KGRN "tsdiff4 = %lld\n" RESET, tsdiff4);
            }

            fprintf (stderr, KRED "diff1 : %llu\n" RESET, ts1 - prev1);
            fprintf (stderr, KRED "diff2 : %llu\n" RESET, ts2 - prev2);
            fprintf (stderr, KRED "diff3 : %llu\n" RESET, ts3 - prev3);
            fprintf (stderr, KRED "diff4 : %llu\n" RESET, ts4 - prev4);
            

            // while (tsdiff2 > 0){
            //     spead2::recv::heap fh1 = stream1.pop();
            //     items1 = fh1.get_items();
            //     ts2 = *((unsigned long long *)items1[0].ptr);
            //     tsdiff2 = ts2 < ts1? ts1 - ts2 : - static_cast< long long >( ts2 - ts1 );
            //     fprintf (stderr, KGRN "ts2 = %llu\n" RESET, ts2);
            //     fprintf (stderr, KGRN "tsdiff2 = %lld\n" RESET, tsdiff2);
            // }
            // while (tsdiff3 > 0){
            //     spead2::recv::heap fh2 = stream2.pop();
            //     items2 = fh2.get_items();
            //     ts3 = *((unsigned long long *)items2[0].ptr);
            //     tsdiff3 = ts3 < ts1? ts1 - ts3 : - static_cast< long long >( ts3 - ts1 );
            //     fprintf (stderr, KGRN "ts3 = %llu\n" RESET, ts3);
            //     fprintf (stderr, KGRN "tsdiff3 = %lld\n" RESET, tsdiff3);
            // }
            // while (tsdiff4 > 0){
            //     spead2::recv::heap fh3 = stream3.pop();
            //     items3 = fh3.get_items();
            //     ts4 = *((unsigned long long *)items3[0].ptr);
            //     tsdiff4 = ts4 < ts1? ts1 - ts4 : - static_cast< long long >( ts4 - ts1 );
            //     fprintf (stderr, KGRN "ts4 = %llu\n" RESET, ts4);
            //     fprintf (stderr, KGRN "tsdiff4 = %lld\n" RESET, tsdiff4);
            // }
            // fprintf(stderr, "YOLO\n");

            // beamform_4 ((uint16_t *)hdu->data_block->curbuf, (uint16_t *) items1[1].ptr, (uint16_t *) items2[1].ptr, (uint16_t *) items3[1].ptr, beamformed, num_vals);

            ssize_t size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);
            // dada_hdu_unlock_read(hdu);

            free(accumulated);
            free(beamformed);
            // free()

        }
        catch (spead2::ringbuffer_stopped &e)
        {
            break;
        }
    }
}

int main (int argc, char **argv)
{
    dada_hdu_t * hdu;
    connect_to_buffer(&hdu, DADA_BUF_1);
    run_ringbuffered (7160, 7161, 7162, hdu);
}