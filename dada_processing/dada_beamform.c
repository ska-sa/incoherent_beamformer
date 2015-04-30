#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>

#include "dada_hdu.h"
#include "dada_def.h"
#include "ascii_header.h"

#define DADA_BUF_1 0x1234
#define DADA_BUF_2 0x2345

#define ACCUMULATE 256
#define N_CHANS 1024

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define RESET "\033[0m"

ipcbuf_t* data_block_1, data_block_2; 
key_t key_1, key_2;

void connect_to_buffer (dada_hdu_t ** hdu, unsigned int dada_buf_key){
    fprintf(stderr, "yo\n");
    multilog_t* log;
    
    char* buffer;

    fprintf(stderr, "yo\n");

    log = multilog_open ("dada_simple_writer", 0);
    multilog_add (log, stderr);

    *hdu = dada_hdu_create(log);

    fprintf (stderr, KRED "dada_buf_id = %x\n" RESET, dada_buf_key);

    dada_hdu_set_key (*hdu, DADA_BUF_1);

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

void beamform(dada_hdu_t * hdu1, dada_hdu_t * hdu2)
{
    if (dada_hdu_lock_read (hdu1) < 0){
         fprintf(stderr, KRED "CONNECT FAILED\n" RESET);
         // return EXIT_FAILURE;
    }

    fprintf (stderr, "yolo\n");
    unsigned char* header1, buffer1, header2, buffer2;
    fprintf (stderr, "yolo\n");
    uint64_t read;
    uint64_t blockid;
    fprintf (stderr, KGRN "hdu1->header_block_key = %x\n" RESET, hdu1->header_block_key);
    // ipcbuf_connect(hdu1->header_block, hdu1->header_block_key);
    header1 = ipcbuf_get_next_read (hdu1->header_block, & read);
    fprintf (stderr, KGRN "read = %" PRIu64 "\n" RESET, read);
    // hexdump (header1, read, 16);
    char utcstart[64];
    ascii_header_get(header1, "UTC_START", "%s", utcstart);
    fprintf(stderr, "utcstart = %s\n", utcstart);
    if (ipcio_is_open (hdu1->data_block)){
        fprintf (stderr, KGRN "OPEN\n" RESET);
    }
    

    fprintf(stderr, "hdu1->data_block->curbufsz = %" PRIu64 "\n", hdu1->data_block->curbufsz);
    // while(1){
        uint8_t* accumulated;
        
        buffer1 = ipcio_open_block_read(hdu1->data_block, &hdu1->data_block->curbufsz, &blockid);
        // fprintf(stderr, "blockid= %x\n", blockid);
        
        // fprintf(stderr, "size= %llu\n", size);
    // }
        accumulate (hdu1->data_block->curbuf, accumulated, hdu1->data_block->curbufsz);
        ssize_t size =  ipcio_close_block_read(hdu1->data_block, hdu1->data_block->curbufsz);
        dada_hdu_unlock_read(hdu1);

        if (dada_hdu_lock_read (hdu2) < 0){
         fprintf(stderr, KRED "CONNECT FAILED\n" RESET);
    }
    fprintf (stderr, "YOLO\n");
    dada_hdu_unlock_read(hdu2);

}

void accumulate (unsigned char * incoming, uint8_t* accumulated, uint64_t size){
    uint64_t accumulated_size = size/N_CHANS/2;
    if (size/N_CHANS/2 % ACCUMULATE != 0){
        fprintf(stderr, KRED "Accumulation period doesn't divide into dada_buffer size");
    }
    fprintf (stderr, "ACCUMULATE\n");

    accumulated = (uint8_t*)malloc(size * 2 / ACCUMULATE);
    memset(accumulated,0,size*2 / ACCUMULATE);

    int i, j, k;
    fprintf (stderr, "ACCUMULATE\n");
    for (i = 0; i < accumulated_size; i= i + ACCUMULATE){
        // accumulated[i] = (uint8_t*)malloc(sizeof(uint8_t) * N_CHANS * 2);
        
        for (k = 0; k < ACCUMULATE; k++){
            for (j = 0; j < N_CHANS; j++){
                // fprintf (stderr, "i = %d, k = %d, j = %d\n", i, k, j);
                accumulated[i * N_CHANS + 2 * j] += (incoming[(i + k) * N_CHANS + j] >> 4) & 15;
                accumulated[i * N_CHANS + 2 * j + 1] += incoming[(i + k) * N_CHANS + j] & 15;
            }
        }
    }
    fprintf (stderr, "i = %d, k = %d, j = %d\n", i, k, j);
    fprintf (stderr, "size = %llu\n", size);
    fprintf (stderr, "(i + k) * N_CHANS + j = %d\n", (i + k) * N_CHANS + j);
}


int main (int argc, char **argv)
{
    fprintf(stderr, "yo\n");
    dada_hdu_t * hdu1;
    dada_hdu_t * hdu2;
    connect_to_buffer(&hdu1, DADA_BUF_2);
    connect_to_buffer(&hdu2, DADA_BUF_1);
    beamform(hdu1,hdu2);
}