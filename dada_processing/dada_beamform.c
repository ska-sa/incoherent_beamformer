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

#include "dada_beamform.h"

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

//SPEAD recieve stuff
// #include "common_thread_pool.h"
// #include <chrono>
#include "spead2/recv_udp.h"
#include "spead2/recv_heap.h"
#include "spead2/recv_live_heap.h"
#include "spead2/recv_ring_stream.h"

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

    // if (dada_hdu_lock_read (*hdu) < 0){
    //     fprintf(stderr, KRED "CONNECT FAILED\n" RESET);
    //     // return EXIT_FAILURE;
    // }

    fprintf(stderr, KGRN "yo\n" RESET);

    // uint64_t block_id;
    // buffer = ipcio_open_block_read (hdu->data_block, 2147483648,  &block_id);
}

/**
 * Write contents of a buffer in hex with ascii characters.
 * Essentially designed to look like tcpdump output.
 * @param pointer buffer        Buffer to be written
 * @param unsigned long index   Number of bytes to write
 * @param unsigned long width   Width of each line in bytes
 */
void hexdump(unsigned char *buffer, unsigned long index, unsigned long width)
{
    unsigned long i = 0;
    int j;
    printf("\n%08lx\t:",i);
    if (index > width){
        
        int j;
        for (j = 0; j < index - width; j=j+width){
            for (i=j;i<j+width;i++)
            {
                printf("%02x ",buffer[i]);
            }
            printf("\t");
            for (i=j;i<j+width;i++)
            {
                if ('!' < buffer[i] && buffer[i] < '~')
                    printf("%c",buffer[i]);
                else
                    printf(".");
            }
            printf("\n%08lx\t:",i);
        }
        for (i;i<index; i++)
        {
            printf("%02x ",buffer[i]);
        }
    }
    else{
        for (i;i<index; i++)
        {
            printf("%02x ",buffer[i]);
        }
    }
}

unsigned long long get_timestamp(dada_hdu_t * hdu){
    char* header;
    uint64_t read;
    header = ipcbuf_get_next_read (hdu->header_block, &read);
    // fprintf (stderr, KGRN "read = %" PRIu64 "\n" RESET, read);
    // hexdump (header, read, 16);
    unsigned long long ts;
    // char utcstart[64];
    ascii_header_get(header, "TS", "%llu", &ts);
    ipcbuf_mark_cleared(hdu->header_block);
    return ts;
}

void initial_header(dada_hdu_t * hdu){
    char* header;
    uint64_t read;
    header = ipcbuf_get_next_read (hdu->header_block, &read);
    fprintf (stderr, KGRN "read = %" PRIu64 "\n" RESET, read);
    // hexdump (header1, read, 16);
    char utcstart[64];
    ascii_header_get(header, "UTC_START", "%s", utcstart);
    fprintf(stderr, "utcstart = %s\n", utcstart);

    ipcbuf_mark_cleared(hdu->header_block);
}

int accumulate (char * incoming, uint16_t* accumulated, uint64_t size){
    //accumulate values in incoming returns the length of accumulated array
    //Accumulated array contains int8_t with 8bit real, 8bit imaginary

    int num_vals = N_CHANS * N_POLS;  //Number of 8bit values per incoming spectra
    // int num_out_vals = num_vals * 4; //Number of 8bit values per outgoing accumulation

    uint64_t num_spectra = size/num_vals;

    if (num_spectra % ACCUMULATE != 0){
        fprintf(stderr, KRED "Accumulation period doesn't divide into dada_buffer size");
    }
    fprintf (stderr, "ACCUMULATE\n");

    uint64_t num_out_vals = num_spectra * N_CHANS * 4 / ACCUMULATE;

    uint64_t num_accs = num_spectra / ACCUMULATE;

    // accumulated = (uint16_t*)malloc(num_out_vals * sizeof(uint16_t));
    memset(accumulated,0,num_out_vals * sizeof(uint16_t));

    int i, j, k;
    fprintf (stderr, "ACCUMULATE\n");

    fprintf (stderr, "accumulated_size/ACCUMULATE = %d\n", ACCUMULATE);

    omp_set_num_threads(4);
    //TODO, Need to deal with bit growth, if all values are 1 then accumulating 256
    //will grow the number out of a uint8, should use a uint16 and shift right 8 bits after accumulation?
    #pragma omp parallel for private(j) private(k)  //openmp does not help (seems to cancel out -O3 flag)
    for (i = 0; i < num_accs; i++){
        // accumulated[i] = (uint8_t*)malloc(sizeof(uint8_t) * N_CHANS * 2);
        int pos = i * ACCUMULATE;
        for (k = 0; k < ACCUMULATE; k++){
            #pragma omp //parallel for
            for (j = 0; j < num_vals/2; j = j + 4){
                uint8_t xRe, xIm, yRe, yIm;
                int p = (pos+k) * num_vals + 2 * j;

                xRe = (incoming[p] >> 4) & 15; //REAL X
                xIm = incoming[p] & 15; //IM X
                yRe = (incoming[p + 2] >> 4) & 15; //REAL Y
                yRe = incoming[p + 2] >> 4; //IM Y

                p = i*num_vals*2+j*4;

                accumulated[p] = accumulated[p] + xRe * xRe;
                accumulated[p + 1] = accumulated[p+1] + yRe * yRe;
                accumulated[p + 2] = accumulated[p+2] + xRe * yRe;
                accumulated[p + 3] = accumulated[p+3] + xIm * yIm;
                
                //handle the weird order of incoming data
                p = (pos+k) * num_vals + 2 * j + 2;
                xRe = (incoming[p] >> 4) & 15; //REAL X
                xIm = incoming[p] & 15; //IM X
                yRe = (incoming[p + 2] >> 4) & 15; //REAL Y
                yRe = incoming[p + 2] >> 4; //IM Y

                p = i*num_vals*2+j*4 + 4;
                accumulated[p] = accumulated[p] + xRe * xRe;
                accumulated[p + 1] = accumulated[p+1] + yRe * yRe;
                accumulated[p + 2] = accumulated[p+2] + xRe * yRe;
                accumulated[p + 3] = accumulated[p+3] + xIm * yIm;
            }
        }
    }
    fprintf (stderr, "i = %d, k = %d, j = %d\n", i, k, j);
    fprintf (stderr, "size = %llu\n", size);
    fprintf (stderr, "(i + k) * N_CHANS + j = %d\n", (i + k) * N_CHANS + j);
    return num_out_vals;
}

void beamform (u_int16_t * acc1, u_int16_t * acc2, u_int16_t * beamformed, uint64_t num_vals){
    int i;
    // beamformed = (uint16_t*)malloc(num_vals * sizeof(uint16_t));
    for (i = 0; i < num_vals; i++)
    {
        beamformed[i] = acc1[i] + acc2[i];
    }
}

void accumulate_and_beamform (char * incoming1, char * incoming2, uint16_t* beamformed, uint64_t size){
    uint16_t *  acc1, * acc2;
    int num_vals;

    uint64_t num_out_vals = size / N_CHANS * N_POLS * N_CHANS * 4 / ACCUMULATE;
    acc1 = (uint16_t*)malloc(num_out_vals * sizeof(uint16_t));
    acc2 = (uint16_t*)malloc(num_out_vals * sizeof(uint16_t));
    
    fprintf (stderr, "----------------BUFFER 1----------------\n");
    num_vals = accumulate(incoming1, acc1, size);
    fprintf (stderr, "----------------BUFFER 2----------------\n");
    accumulate (incoming2, acc2, num_vals);
    fprintf (stderr, "----------------BEAMFORM----------------\n");
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
void spead_out(uint16_t * beamformed, unsigned long long ts, uint64_t num_vals, spead2::send::udp_stream * stream)
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
    h.add_item(0x1001, beamformed, sizeof(uint16_t) * num_vals, true);
    h.add_descriptor(desc1);
    h.add_descriptor(desc2);

    fprintf (stderr, KYEL "sizeof(uint16_t) * num_vals = %llu\n" RESET, sizeof(uint16_t) * num_vals);

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

void consume(dada_hdu_t * hdu1, dada_hdu_t * hdu2)
{

    spead2::thread_pool tp;
    udp::resolver resolver(tp.get_io_service());
    udp::resolver::query query("192.168.64.217", "7162");
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
         fprintf(stderr, KRED "hdu1 CONNECT FAILED\n" RESET);
         // return EXIT_FAILURE;
    }
    initial_header(hdu1);
    initial_header(hdu2);
    
    if (ipcio_is_open (hdu1->data_block)){
        fprintf (stderr, KGRN "OPEN\n" RESET);
    }
    
    // fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", hdu1->data_block->curbufsz);
    while(1){

        int init = 0;

        if (buffer_allignment == 13){
            ts1 = get_timestamp(hdu1);
            ts2 = get_timestamp(hdu2);
            

            fprintf(stderr, "ts1 = %llu\n", ts1);
            fprintf(stderr, "ts2 = %llu\n", ts2);

            fprintf(stderr, KGRN "ts diff of %llu\n" RESET, ts2 - ts1);

            if (ts1 > ts2){
                buffer_allignment = (ts1-ts2)/TIMESTAMP_INCREMENT*heap_size;
                alligned = &hdu2;
                missalligned = &hdu1;
            }
            else{
                buffer_allignment = (ts2-ts1)/TIMESTAMP_INCREMENT*heap_size;
                alligned = &hdu1;
                missalligned = &hdu2;
            }
            dada_hdu_unlock_read(hdu1);
            dada_hdu_unlock_read(hdu2);
        }

        fprintf(stderr, KGRN "Buffer allignment of %llu\n" RESET, buffer_allignment);

        if (dada_hdu_lock_read (*alligned) < 0){
            fprintf(stderr, KRED "CONNECT FAILED\n" RESET);
             // return EXIT_FAILURE;
        }
        if (dada_hdu_lock_read (*missalligned) < 0){
            fprintf(stderr, KRED "CONNECT FAILED\n" RESET);
             // return EXIT_FAILURE;
        }

        uint16_t* beamformed;
        char* buffer1, *buffer2;
        char* align_buffer;
        uint64_t blockid1, blockid2;
        fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", (*missalligned)->data_block->curbufsz);

        buffer1 = ipcio_open_block_read((*alligned)->data_block, &((*alligned)->data_block->curbufsz), &blockid1);

        buffer2 = ipcio_open_block_read((*missalligned)->data_block, &((*missalligned)->data_block->curbufsz), &blockid2);

        fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", (*missalligned)->data_block->curbufsz);

        fprintf(stderr, "YOLO\n");

        // if (init == 0){ //first buffer
            fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", (*missalligned)->data_block->curbufsz);
            align_buffer = (char*)malloc((*missalligned)->data_block->curbufsz);
            fprintf(stderr, "YOLOin\n");
            memset(align_buffer, 0, (*missalligned)->data_block->curbufsz);
            fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", (*missalligned)->data_block->curbufsz);
            init = 1;
        // }

        fprintf(stderr, "YOLO\n");

        fprintf(stderr, "buffer_allignment = %llu. (*missalligned)->data_block->curbufsz = %" PRIu64 " (*missalligned)->data_block->curbufsz - buffer_allignment = %llu\n", buffer_allignment, (*missalligned)->data_block->curbufsz, (*missalligned)->data_block->curbufsz - buffer_allignment);

        memcpy (align_buffer + buffer_allignment, (*missalligned)->data_block->curbuf, (*missalligned)->data_block->curbufsz - buffer_allignment);

        clock_t start = clock(), diff;

        double wstart = omp_get_wtime();
        beamformed = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);

        accumulate_and_beamform ((*alligned)->data_block->curbuf, align_buffer, beamformed, (*alligned)->data_block->curbufsz);
        // accumulate (buffer1, accumulated, hdu1->data_block->curbufsz);

        memcpy (align_buffer, (*missalligned)->data_block->curbuf + (*missalligned)->data_block->curbufsz - buffer_allignment, buffer_allignment);
        int num_out_vals = (*alligned)->data_block->curbufsz / N_CHANS * N_POLS * N_CHANS * 4 / ACCUMULATE;
        diff = clock() - start;
        double wdiff = omp_get_wtime() - wstart; 
        int msec = diff * 1000 / CLOCKS_PER_SEC;
        fprintf(stderr, "Time taken %d seconds %d milliseconds\n", msec/1000, msec%1000);
        fprintf(stderr, "Wall time taken %f seconds\n", wdiff);
        fprintf(stderr, KGRN "Speed up of %f\n" RESET, diff/1000000/wdiff);

        ts1 = get_timestamp((*alligned));
        ts2 = get_timestamp((*missalligned));

        ssize_t size =  ipcio_close_block_read((*alligned)->data_block, (*alligned)->data_block->curbufsz);
        dada_hdu_unlock_read((*alligned));

        size =  ipcio_close_block_read((*missalligned)->data_block, (*missalligned)->data_block->curbufsz);
        dada_hdu_unlock_read((*missalligned));

        
        fprintf (stderr, KRED "num_out_vals = %llu\n" RESET, num_out_vals);
        spead_out (beamformed, ts1, num_out_vals, &stream);
        fprintf (stderr, KGRN "out\n" RESET);

        free(align_buffer);
        free(beamformed);
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
    consume(hdu1,hdu2);
}
