#include "dada_simple_writer.h"

#include "dada_hdu.h"
#include "dada_def.h"
#include "ascii_header.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>

dada_hdu_t * hdu;

// void usage()
// {
//   fprintf (stdout,
// 	   "dada_simple_writer [options]\n"
//      " -h   print this help text\n"
//      " -k   hexadecimal shared memory key  [default: %x]\n"
//      " -f   file to write to the ring buffer \n"
//      " -o bytes  number of bytes to seek into the file\n"
//      " -s   single file then exit\n"
//      " -d   run as daemon\n"
//      " -z   use zero copy shm access\n", DADA_DEFAULT_BLOCK_KEY);
// }

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
    return -1;
  }

  // now write all the required key/value pairs to the header. Some 
  // examples shown below
  char buffer[64];
  sprintf (buffer, "%02d:%02d:%02d.%d", 4, 37, 15, 883250);
  if (ascii_header_set (hdu->header, "RA", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write RA to header\n");
    return -1;
  }

  sprintf (buffer, "%02d:%02d:%02d.%d", -47, 15, 9, 31863);
  if (ascii_header_set (hdu->header, "DEC", "%s", buffer) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write DEC to header\n");
    return -1;
  }

  float tsamp = 0.005;
  if (ascii_header_set (hdu->header, "TSAMP", "%f", tsamp) < 0)
  {
    multilog (hdu->log, LOG_WARNING, "Could not write TSAMP to header\n");
    return -1;
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

//
// extremely simple (non useful) code to write to the data block
// the techniques below use the simple or the efficient interface
// to the ring buffer
//
int simple_writer_write (dada_hdu_t * hdu, char * input, uint64_t block_size)
{
  // char * buffer;
  // if (!zero_copy)
  // {
    // buffer is some memory you have earlier allocated and 
    // something has filled it with data you wish to write to 
    // the data block
    char * buffer;
    size_t buffer_size;
    size_t bytes_written;
  //   ipcio_write (hdu->data_block, buffer, buffer_size); 
  // }
  // else
  // {
    uint64_t block_id;
    buffer = ipcio_open_block_write (hdu->data_block, &block_id);
    if (!buffer)
    {
      multilog (hdu->log, LOG_ERR, "write: ipcio_open_block_write failed\n");
      return -1;
    }

    // uint64_t block_size = ipcbuf_get_bufsz ((ipcbuf_t *) hdu->data_block);

    // fill buffer with block_size bytes - should fill it completely, unless
    // this is the very end of the data stream for the observation
     memcpy (buffer, input, block_size);
      

  // }
  return 0;
}

void simple_writer_close_hdu (dada_hdu_t * hdu, uint64_t block_size){
  ipcio_close_block_write (hdu->data_block, block_size);
}

void simple_writer_connect_hdu (dada_hdu_t * hdu, key_t dada_key){
  // PSRDada logging utility
  multilog_t* log = multilog_open ("dada_simple_writer", 0);
  multilog_add (log, stderr);

  // create PSRDADA header + dada struct
  hdu = dada_hdu_create (log);

  // set the key (this should match the key used to create the SMRB with dada_db -k XXXX)
  dada_hdu_set_key(hdu, dada_key);

  // connect to the SMRB
  if (dada_hdu_connect (hdu) < 0)
    fprintf(stderr, "couldn't connect");
    // return EXIT_FAILURE;

  // obtain the writer lock on this SMRB
  // if (dada_hdu_lock_write (hdu) < 0)
  //   return EXIT_FAILURE;

}

void to_dada_buffer (dada_hdu_t * hdu)  //SHould only be called by master thread
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
    // memcpy(buffer + order_buffer_tail, order_buffer + o_b_off, obSegment);
    // simple_writer_write(hdu, order_buffer + o_b_off, obSegment);
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


// int main (int argc, char **argv)
// {
//   fprintf(stderr, "open\n");
//   connect_hdu(circular_buf, 0x1234);
//   fprintf(stderr, "connected\n");

//   return EXIT_SUCCESS;
// }
