#include "dada_beamform.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <omp.h>
#include <time.h>
#include  <signal.h>

//SPEAD send stuff
#include <iostream>
#include <utility>
#include <endian.h>
#include <boost/asio.hpp>
#include "spead2/common_defines.h"
#include "spead2/common_thread_pool.h"

#include "spead2/common_flavour.h"
#include "spead2/send_heap.h"
#include "spead2/send_udp.h"
#include "spead2/send_stream.h"

// //SPEAD recieve stuff
// // #include "common_thread_pool.h"
// // #include <chrono>
// #include "spead2/recv_udp.h"
// #include "spead2/recv_heap.h"
// #include "spead2/recv_live_heap.h"
// #include "spead2/recv_ring_stream.h"

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define RESET "\033[0m"

using boost::asio::ip::udp;


ipcbuf_t* data_block_1, data_block_2; 
key_t key_1, key_2;
spead2::send::udp_stream * stream_p;

void accumulate_and_beamform (char * incoming1, char * incoming2, int16_t* beamformed, uint64_t size){
    int16_t *  acc1, * acc2;
    int num_vals;

    uint64_t num_out_vals = size * 2 / ACCUMULATE;
    acc1 = (int16_t*)malloc(num_out_vals * sizeof(int16_t));
    acc2 = (int16_t*)malloc(num_out_vals * sizeof(int16_t));
    
    // fprintf (stderr, "----------------BUFFER 1----------------\n");
    num_vals = accumulate(incoming1, acc1, size);
    // fprintf (stderr, "----------------BUFFER 2----------------\n");
    accumulate (incoming2, acc2, size);
    // fprintf (stderr, "----------------BEAMFORM----------------\n");
    beamform (acc1, acc2, beamformed, num_vals);

    free(acc1);
    free(acc2);
}

/**
 * Send out beamformed data in a spead stream
 * @beamformed array of accumulated and beamformed data
 * @ts timestamp for this heap
 * @ts num_vals number of values in beamformed array
 * @tp spead2 stream for sending data
 */
void spead_out(int16_t * beamformed, unsigned long long ts, uint64_t num_vals, spead2::send::udp_stream * stream)
{
    fprintf (stderr, KRED "YOHO\n" RESET);
    //SPEAD STREAM SET UP

    fprintf (stderr, KGRN "HEY\n" RESET);

    spead2::flavour f(spead2::maximum_version, 64, 48, spead2::BUG_COMPAT_PYSPEAD_0_5_2);
    spead2::send::heap h(0x2, f);
    spead2::descriptor desc1;
    desc1.id = 0x1000;
    desc1.name = "ADC_COUNT";
    desc1.description = "a scalar int";
    desc1.format.emplace_back('i', 64);

    fprintf (stderr, KGRN "HEY\n" RESET);

    spead2::descriptor desc2;
    desc2.id = 0x1001;
    desc2.name = "DATA";
    desc2.description = "a 1D array of beamformed data";
    char buffer[64];
    sprintf(buffer, "{'shape': (%llu), 'fortran_order': False, 'descr': 'i2'}", num_vals);
    desc2.numpy_header = buffer;

    fprintf (stderr, KGRN "num_vals = %llu\n" RESET, num_vals);
    h.add_item(0x1000, &ts, sizeof(unsigned long long), true);
    h.add_item(0x1001, beamformed, sizeof(int16_t) * num_vals, true);
    h.add_descriptor(desc1);
    h.add_descriptor(desc2);

    fprintf (stderr, KYEL "sizeof(int16_t) * num_vals = %llu\n" RESET, sizeof(int16_t) * num_vals);

    (*stream).async_send_heap(h, [] (const boost::system::error_code &ec, spead2::item_pointer_t bytes_transferred)
    {
        if (ec)
            std::cerr << ec.message() << '\n';
        else
            std::cout << "Sent " << bytes_transferred << " bytes in heap\n";
    });

    // spead2::send::heap end(0x3, f);
    // end.add_end();
    // (*stream).async_send_heap(end, [] (const boost::system::error_code &ec, spead2::item_pointer_t bytes_transferred) {});
    (*stream).flush();
}

void consume(dada_hdu_t * hdu1, dada_hdu_t * hdu2, char* port, char* ip)
{

    spead2::thread_pool tp;
    udp::resolver resolver(tp.get_io_service());
    udp::resolver::query query(ip, port);
    auto it = resolver.resolve(query);
    spead2::send::udp_stream stream(tp.get_io_service(), *it, spead2::send::stream_config(9000,  67108864 * 1.5));
    stream_p = &stream;

    unsigned long long ts1, ts2;

    uint64_t heap_size = N_POLS * N_CHANS * BYTES_PER_SAMPLE * TIMESTAMPS_PER_HEAP;
    uint64_t buffer_allignment = 13; //Never going to be 13, could be 0

    dada_hdu_t ** alligned, ** missalligned;

    if (dada_hdu_lock_read (hdu1) < 0){
         fprintf(stderr, KRED "hdu1 CONNECT FAILED\n" RESET);
         // return EXIT_FAILURE;
    }

    if (dada_hdu_lock_read (hdu2) < 0){
         fprintf(stderr, KRED "hdu2 CONNECT FAILED\n" RESET);
         // return EXIT_FAILURE;
    }
    initial_header(hdu1);
    initial_header(hdu2);
    
    if (ipcio_is_open (hdu1->data_block)){
        fprintf (stderr, KGRN "OPEN\n" RESET);
    }

    unsigned long long prev;
    uint64_t blockid1, blockid2;

    uint64_t num_occupied_1 = ipcbuf_get_write_count (&(hdu1->data_block->buf)) - ipcbuf_get_read_count (&(hdu1->data_block->buf));
    uint64_t num_occupied_2 = ipcbuf_get_write_count (&(hdu2->data_block->buf)) - ipcbuf_get_read_count (&(hdu2->data_block->buf));

    while (num_occupied_1 > 0 && num_occupied_2 > 0){
        // fprintf (stderr, KYEL "hdu1 write count = %llu and hdu2 write count = %llu\n" RESET, ipcbuf_get_write_count (&(hdu1->data_block->buf)), ipcbuf_get_write_count (&(hdu1->data_block->buf)));
        get_timestamp(hdu1);
        get_timestamp(hdu2);
        ipcio_open_block_read(hdu1->data_block, &(hdu1->data_block->curbufsz), &blockid1);
        ipcio_open_block_read(hdu2->data_block, &(hdu2->data_block->curbufsz), &blockid2);
        ssize_t size =  ipcio_close_block_read(hdu1->data_block, hdu1->data_block->curbufsz);
        dada_hdu_unlock_read(hdu1);
        size =  ipcio_close_block_read(hdu2->data_block, hdu2->data_block->curbufsz);
        dada_hdu_unlock_read(hdu2);

        if (dada_hdu_lock_read (hdu1) < 0){
            fprintf(stderr, KRED "hdu1 CONNECT FAILED\n" RESET);
            // return EXIT_FAILURE;
        }

        if (dada_hdu_lock_read (hdu2) < 0){
            fprintf(stderr, KRED "hdu2 CONNECT FAILED\n" RESET);
            // return EXIT_FAILURE;
        }

        num_occupied_1 = ipcbuf_get_write_count (&(hdu1->data_block->buf)) - ipcbuf_get_read_count (&(hdu1->data_block->buf));
        num_occupied_2 = ipcbuf_get_write_count (&(hdu2->data_block->buf)) - ipcbuf_get_read_count (&(hdu2->data_block->buf));
    }

    fprintf (stderr, KGRN "Cleared all buffers\n" RESET);
    int init = 0;
    
    // fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", hdu1->data_block->curbufsz);
    while(1){

        

        if (buffer_allignment == 13){
            ts1 = get_timestamp(hdu1);
            ts2 = get_timestamp(hdu2);
            

            fprintf(stderr, "ts1 = %llu\n", ts1);
            fprintf(stderr, "ts2 = %llu\n", ts2);

            fprintf(stderr, KGRN "ts diff of %llu\n" RESET, ts2 - ts1);

            if (ts1 > ts2){
                fprintf(stderr, "ts off from acc = %llu", ts1%(ACCUMULATE*TIMESTAMP_INCREMENT));
                buffer_allignment = (ts1-ts2)/TIMESTAMP_INCREMENT*heap_size;
                alligned = &hdu2;
                missalligned = &hdu1;
            }
            else{
                fprintf(stderr, "ts off from acc = %llu", ts1%(ACCUMULATE*TIMESTAMP_INCREMENT));
                buffer_allignment = (ts2-ts1)/TIMESTAMP_INCREMENT*heap_size;
                alligned = &hdu1;
                missalligned = &hdu2;
            }
            dada_hdu_unlock_read(hdu1);
            dada_hdu_unlock_read(hdu2);
        }

        // fprintf(stderr, KGRN "Buffer allignment of %llu\n" RESET, buffer_allignment);

        if (dada_hdu_lock_read (*alligned) < 0){
            fprintf(stderr, KRED "CONNECT FAILED\n" RESET);
             // return EXIT_FAILURE;
        }
        if (dada_hdu_lock_read (*missalligned) < 0){
            fprintf(stderr, KRED "CONNECT FAILED\n" RESET);
             // return EXIT_FAILURE;
        }

        int16_t** beamformed, *beam1, *beam2;
        int buff_count = 0;
        char* buffer1, *buffer2;
        char* align_buffer;
        // fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", (*missalligned)->data_block->curbufsz);

        buffer1 = ipcio_open_block_read((*alligned)->data_block, &((*alligned)->data_block->curbufsz), &blockid1);

        buffer2 = ipcio_open_block_read((*missalligned)->data_block, &((*missalligned)->data_block->curbufsz), &blockid2);

        // fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", (*missalligned)->data_block->curbufsz);

        // fprintf(stderr, "YOLO\n");

        uint64_t num_out_vals = (*missalligned)->data_block->curbufsz * 2 / ACCUMULATE;
        if (init == 0){ //first buffer
            fprintf (stderr, "MAKING ALIGN BUFFER\n");

            align_buffer = (char*)malloc((*missalligned)->data_block->curbufsz);
            beam1 = (int16_t *)malloc(sizeof(int16_t) * num_out_vals);
            beam2 = (int16_t *)malloc(sizeof(int16_t) * num_out_vals);
            // fprintf(stderr, "YOLOin\n");
            memset(align_buffer, 0, (*missalligned)->data_block->curbufsz);
            // fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", (*missalligned)->data_block->curbufsz);
            init = 1;
        }

        // fprintf(stderr, "YOLO\n");

        // fprintf(stderr, "buffer_allignment = %llu. (*missalligned)->data_block->curbufsz = %" PRIu64 " (*missalligned)->data_block->curbufsz - buffer_allignment = %llu\n", buffer_allignment, (*missalligned)->data_block->curbufsz, (*missalligned)->data_block->curbufsz - buffer_allignment);

        memcpy (align_buffer + buffer_allignment, (*missalligned)->data_block->curbuf, (*missalligned)->data_block->curbufsz - buffer_allignment);

        clock_t start = clock(), diff;

        double wstart = omp_get_wtime();

        

        if(buff_count == 0){
            //beam1 = (int16_t *)malloc(sizeof(int16_t) * 67108864);
            accumulate_and_beamform ((*alligned)->data_block->curbuf, align_buffer, beam1, (*alligned)->data_block->curbufsz);
        }
        else{
            //beam2 = (int16_t *)malloc(sizeof(int16_t) * 67108864);
            accumulate_and_beamform ((*alligned)->data_block->curbuf, align_buffer, beam2, (*alligned)->data_block->curbufsz);
        }
        // accumulate (buffer1, accumulated, hdu1->data_block->curbufsz);


        memcpy (align_buffer, (*missalligned)->data_block->curbuf + (*missalligned)->data_block->curbufsz - buffer_allignment, buffer_allignment);
        // int num_out_vals = (*alligned)->data_block->curbufsz / N_CHANS * N_POLS * N_CHANS * 4 / ACCUMULATE;
        diff = clock() - start;
        double wdiff = omp_get_wtime() - wstart; 
        int msec = diff * 1000 / CLOCKS_PER_SEC;
        // fprintf(stderr, "Time taken %d seconds %d milliseconds\n", msec/1000, msec%1000);
        // fprintf(stderr, "Wall time taken %f seconds\n", wdiff);
        // fprintf(stderr, KGRN "Speed up of %f\n" RESET, diff/1000000/wdiff);

        ts1 = get_timestamp((*alligned));
        ts2 = get_timestamp((*missalligned));

        ssize_t size =  ipcio_close_block_read((*alligned)->data_block, (*alligned)->data_block->curbufsz);
        dada_hdu_unlock_read((*alligned));

        size =  ipcio_close_block_read((*missalligned)->data_block, (*missalligned)->data_block->curbufsz);
        dada_hdu_unlock_read((*missalligned));

        
        fprintf (stderr, KGRN "ts = %llu\n" RESET, ts1);
        fprintf (stderr, KRED "diff : %llu\n" RESET, ts1 - prev);
        prev = ts1;
        // spead_out (beamformed, ts1, num_out_vals, &stream);
        stream.flush();

        spead2::flavour f(spead2::maximum_version, 64, 48, spead2::BUG_COMPAT_PYSPEAD_0_5_2);
        spead2::send::heap h(0x2, f);
        spead2::descriptor desc1;
        desc1.id = 0x1000;
        desc1.name = "ADC_COUNT";
        desc1.description = "a scalar int";
        desc1.format.emplace_back('i', 64);

        fprintf (stderr, KGRN "HEY\n" RESET);

        spead2::descriptor desc2;
        desc2.id = 0x2001;
        desc2.name = "DATA";
        desc2.description = "a 1D array of beamformed data";
        char buffer[64];
        sprintf(buffer, "{'shape': (%llu), 'fortran_order': False, 'descr': 'i2'}", num_out_vals);
        desc2.numpy_header = buffer;

        

        fprintf (stderr, KGRN "num_vals = %llu\n" RESET, num_out_vals);
        fprintf (stderr, KYEL "ts1 = %llu\n", ts1);
        h.add_descriptor(desc1);
        h.add_item(0x1000, &ts1, sizeof(ts1), true);

        h.add_descriptor(desc2);
        if (buff_count == 0){
            h.add_item(0x2001, beam1, sizeof(int16_t) * num_out_vals, true);
            // free(beam2);
        }
        else
        {
            h.add_item(0x2001, beam2, sizeof(int16_t) * num_out_vals, true);
            // free(beam1);
        }

        
        

        fprintf (stderr, KYEL "sizeof(int16_t) * num_vals = %llu\n" RESET, sizeof(int16_t) * num_out_vals);

        
        stream.async_send_heap(h, [] (const boost::system::error_code &ec, spead2::item_pointer_t bytes_transferred)
        {
            if (ec)
                std::cerr << ec.message() << '\n';
            else
                std::cout << "Sent " << bytes_transferred << " bytes in heap\n";
        });


        // spead2::send::heap end(0x3, f);
        // end.add_end();
        // (*stream).async_send_heap(end, [] (const boost::system::error_code &ec, spead2::item_pointer_t bytes_transferred) {});
        

        // fprintf (stderr, KGRN "out\n" RESET);

        // free(align_buffer);
        // free(beamformed);

        buff_count = (buff_count + 1) % 2;
        // free(buffer1);
        // free(buffer2);
    }
}

void INThandler(int sig){
    fprintf(stderr, "Sending end of stream packet\n");
    spead2::flavour f(spead2::maximum_version, 64, 48, spead2::BUG_COMPAT_PYSPEAD_0_5_2);
    spead2::send::heap end(0x3, f);
    end.add_end();
    (*stream_p).async_send_heap(end, [] (const boost::system::error_code &ec, spead2::item_pointer_t bytes_transferred) {});
    (*stream_p).flush();
    fprintf(stderr, "Exiting Cleanly\n");
    exit(0);
}

int main (int argc, char **argv)
{
    signal(SIGINT, INThandler);
    dada_hdu_t * hdu1;
    dada_hdu_t * hdu2;
    connect_to_buffer(&hdu1, DADA_BUF_1);
    connect_to_buffer(&hdu2, DADA_BUF_2);
    if (argc == 3)
        consume(hdu1,hdu2, argv[1],argv[2]);
}
