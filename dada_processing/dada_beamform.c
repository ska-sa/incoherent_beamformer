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

int accumulate (char * incoming, int16_t* accumulated, uint64_t size){
    //accumulate values in incoming returns the length of accumulated array
    //Accumulated array contains int8_t with 8bit real, 8bit imaginary

    int num_vals = N_CHANS * N_POLS;  //Number of 8bit values per incoming spectra
    // int num_out_vals = num_vals * 4; //Number of 8bit values per outgoing accumulation

    uint64_t num_spectra = size/num_vals;
    //fprintf(stderr, "num_spectra = %llu\n",num_spectra);
    //fprintf(stderr, " = %llu\n",size);

    if (num_spectra % ACCUMULATE != 0){
        fprintf(stderr, KRED "Accumulation period doesn't divide into dada_buffer size");
    }
    // fprintf (stderr, "ACCUMULATE\n");

    uint64_t num_out_vals = size * 2 / ACCUMULATE;
    //fprintf(stderr, "num_out_vals = %llu\n", num_out_vals);
    uint64_t num_accs = num_spectra / ACCUMULATE;

    // accumulated = (uint16_t*)malloc(num_out_vals * sizeof(uint16_t));
    memset(accumulated,0,num_out_vals * sizeof(uint16_t));

    int i, j, k;
    // fprintf (stderr, "ACCUMULATE\n");

    // fprintf (stderr, "accumulated_size/ACCUMULATE = %d\n", ACCUMULATE);
    // 
    // 
    omp_set_num_threads(4);
    //TODO, Need to deal with bit growth, if all values are 1 then accumulating 256
    //will grow the number out of a uint8, should use a uint16 and shift right 8 bits after accumulation?
    #pragma omp parallel for private(i) private(k)  //openmp does not help (seems to cancel out -O3 flag)
    for (j = 0; j < num_accs; j++){
        int step = j * ACCUMULATE * 2048;
        for (k = 0; k < ACCUMULATE; k++){
            int pos = step + k * 2048; 
            for (i = 0; i < 2048; i = i + 4)
            {
                int8_t xRe, xIm, yRe, yIm;
                int pi = pos + i;           //incoming pos
                int pa = j * 4096 + i * 2;  //accumulation pos

                xRe = incoming[pi] & 240; //REAL X
                xIm = incoming[pi] & 15; //IM X
                yRe = incoming[pi + 2] & 240; //REAL Y
                yIm = incoming[pi + 2] & 15; //IM Y
                

                accumulated[pa] = accumulated[pa] + xRe * xRe;
                accumulated[pa + 1] = accumulated[pa + 1] + yRe * yRe;
                accumulated[pa + 2] = accumulated[pa + 2] + xRe * yRe;
                accumulated[pa + 3] = accumulated[pa + 3] + xIm * yIm;

                xRe = incoming[pi + 1] & 240; //REAL X
                xIm = incoming[pi + 1] & 15; //IM X
                yRe = incoming[pi + 3] & 240; //REAL Y
                yIm = incoming[pi + 3] & 15; //IM Y

                accumulated[pa + 4] = accumulated[pa + 4] + xRe * xRe;
                accumulated[pa + 5] = accumulated[pa + 5] + yRe * yRe;
                accumulated[pa + 6] = accumulated[pa + 6] + xRe * yRe;
                accumulated[pa + 7] = accumulated[pa + 7] + xIm * yIm;
            }
            // fprintf(stderr, "pos = %d\n", pos);
        }
    }

    //fprintf(stderr, "i = %d, j = %d, k = %d . pi = %d, pa = %d\n",i,j,k,(j-1) * ACCUMULATE * 2048 + (k-1) * 2048 + i -1, (j-1) * 4096 + (i-1) * 2);

    // omp_set_num_threads(4);
    // //TODO, Need to deal with bit growth, if all values are 1 then accumulating 256
    // //will grow the number out of a uint8, should use a uint16 and shift right 8 bits after accumulation?
    // #pragma omp parallel for private(j) private(k)  //openmp does not help (seems to cancel out -O3 flag)
    // for (i = 0; i < num_accs; i++){
    //     // accumulated[i] = (uint8_t*)malloc(sizeof(uint8_t) * N_CHANS * 2);
    //     int pos = i * ACCUMULATE;
    //     for (k = 0; k < ACCUMULATE; k++){
    //         #pragma omp //parallel for
    //         for (j = 0; j < num_vals/2; j = j + 4){
    //             uint8_t xRe, xIm, yRe, yIm;
    //             int p = (pos+k) * num_vals + 2 * j;

    //             xRe = (incoming[p] >> 4) & 15; //REAL X
    //             xIm = incoming[p] & 15; //IM X
    //             yRe = (incoming[p + 2] >> 4) & 15; //REAL Y
    //             yIm = incoming[p + 2] >> 4; //IM Y

    //             p = i*num_vals*2+j*4;

    //             accumulated[p] = accumulated[p] + xRe * xRe;
    //             accumulated[p + 1] = accumulated[p+1] + yRe * yRe;
    //             accumulated[p + 2] = accumulated[p+2] + xRe * yRe;
    //             accumulated[p + 3] = accumulated[p+3] + xIm * yIm;
                
    //             //handle the weird order of incoming data
    //             p = (pos+k) * num_vals + 2 * j + 2;
    //             xRe = (incoming[p] >> 4) & 15; //REAL X
    //             xIm = incoming[p] & 15; //IM X
    //             yRe = (incoming[p + 2] >> 4) & 15; //REAL Y
    //             yRe = incoming[p + 2] >> 4; //IM Y

    //             p = i*num_vals*2+j*4 + 4;
    //             accumulated[p] = accumulated[p] + xRe * xRe;
    //             accumulated[p + 1] = accumulated[p+1] + yRe * yRe;
    //             accumulated[p + 2] = accumulated[p+2] + xRe * yRe;
    //             accumulated[p + 3] = accumulated[p+3] + xIm * yIm;
    //         }
    //     }
    // }
    // fprintf (stderr, "i = %d, k = %d, j = %d\n", i, k, j);
    // fprintf (stderr, "size = %llu\n", size);
    // fprintf (stderr, "(i + k) * N_CHANS + j = %d\n", (i + k) * N_CHANS + j);
    return num_out_vals;
}

void beamform (int16_t * acc1, int16_t * acc2, int16_t * beamformed, uint64_t num_vals){
    int i;
    // beamformed = (uint16_t*)malloc(num_vals * sizeof(uint16_t));
    //fprintf (stderr, "num_vals = %llu\n", num_vals);
    for (i = 0; i < num_vals; i++)
    {
        // fprintf(stderr, "%d ,", i);
        beamformed[i] = acc1[i] + acc2[i];
    }
}
