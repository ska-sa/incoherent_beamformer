#include "dada_buffer.h"

key_t dada_key;
int num_readers;
u_int64_t nbufs;
u_int64_t bufsz;
char *buf;

ipcbuf_t header = IPCBUF_INIT;

int page = 0;
int destroy = 0;
int lock = 0;
int consume_flag = 0;
int produce_flag = 0;
int arg;

void usage ()
{
    fprintf (stdout,
             "dada_buffer - create or destroy the DADA shared memory ring buffer\n"
             "              Can also produce test data to put in buffer\n"
             "              Can start a process to consume data from the buffer\n"
             "\n"
             "Usage: dada_db [-d] [-k key] [-n nbufs] [-b bufsz] [-r nreaders]\n"
             " -b  size of each buffer (in bytes) [default: %d]\n"
             " -d  destroy the shared memory area [default: create]\n"
             " -k  hexadecimal shared memory key  [default: %x]\n"
             " -l  lock the shared memory area in physical RAM\n"
             " -n  number of buffers in ring      [default: %d]\n"
             " -p  page all blocks into RAM\n"
             " -r  number of readers              [default: 1]\n"
             " -p  produce test data\n"
             " -c  consume data\n",
             BUFSIZE,
             DADA_KEY,
             NBUFS);
}

/*Access the next buffer in the data_block. The data_block is made of nbufs buffers in memory, this method returns the next readable
buffer*/
int read_next_buf(ipcbuf_t* data_block, char **out)
{
    uint64_t read;
    ipcbuf_lock_read(data_block);
    *out = ipcbuf_get_next_read(data_block, &read);
    ipcbuf_mark_cleared(data_block);
    ipcbuf_unlock_read(data_block);
    return read;
}

/*Mark the current buffer in the data_block as filled at position pos */
void mark_filled (ipcbuf_t* data_block, uint64_t pos)
{
    ipcbuf_lock_write(data_block);
    ipcbuf_mark_filled (data_block, pos);
    ipcbuf_unlock_write(data_block);
}

/*Set the current buffer to a new buffer, the current buffer is teh buffer which will be written to*/
void set_current_buf(ipcbuf_t* data_block, char *new_buf)
{
    buf = new_buf;
}

char *mark_filled_and_get_next_buf(ipcbuf_t* data_block, uint64_t pos)
{
    // while (!ipcbuf_is_writer(data_block)){
    ipcbuf_lock_write(data_block);
    ipcbuf_mark_filled (data_block, pos);
    char *new_buf = ipcbuf_get_next_write(data_block);
    ipcbuf_unlock_write(data_block);
    return new_buf;
}

/*Get the next empty buffer to be written to from the data_block*/
char *get_next_buf (ipcbuf_t* data_block)
{
    ipcbuf_lock_write(data_block);
    char *new_buf = ipcbuf_get_next_write(data_block);
    ipcbuf_unlock_write(data_block);
    return new_buf;
}

/*Write *data of size to the buffer at position pos*/
int write_to_buf (ipcbuf_t* data_block, char* buffer, char *data, uint64_t size)
{
    fprintf(stderr, "WRITING!!!+++++++++++++++++++++++++++++++++++++++++++++++++++++++");

    uint64_t written = 0;
    while (!ipcbuf_is_writer(data_block)){
        fprintf(stderr, "NOT WRITER");
        ipcbuf_lock_write(data_block); //Don't think I need this, but I will remove it once I am  sure
        if (ipcbuf_is_writer(data_block)){
            fprintf(stderr, "NOW WRITER");
        }
    }

    /* write data to datablock */
    memcpy(buffer, data, size);

    // fprintf (stderr, "Wrote %"PRIu64" bytes to buffer at pos %"PRIu64"\n", size, pos);

    ipcbuf_unlock_write (data_block);

    return size;
}

int delete_buffer (ipcbuf_t* data_block)
{

    // ipcbuf_connect (&data_block, dada_key);
    ipcbuf_destroy (data_block);
    fprintf (stderr, "Destroyed DADA data\n");

    // ipcbuf_connect (&header, dada_key + 1);
    // ipcbuf_destroy (&header);

    

    return 0;
}

int connect_to_buffer(ipcbuf_t* data_block, key_t key)
{
    fprintf(stderr, "COPNNECTING!!!+++++++++++++++++++++++++++++++++++++++++++++++++++++++");
    dada_key = key;
    ipcbuf_connect(data_block, key);
    fprintf(stderr, "CONNECTED");
    nbufs = ipcbuf_get_nbufs(data_block);
    bufsz = 0; //ipcbuf_get_bufsz (data_block);
    num_readers = ipcbuf_get_nreaders(data_block);
    fprintf(stderr, "Connected ti buffer with :\n"
            "dada_key = %x\n"
            "num_readers = %d\n"
            "nbufs = %"PRIu64"\n"
            "bufsz = %"PRIu64"\n",
            dada_key, num_readers, nbufs, bufsz);
}

/*Create a buffer*/
int create_buffer(ipcbuf_t* data_block, key_t d_k, int n_r, u_int64_t nb, u_int64_t bs)
{
    // No headers untill I figure out what they are for
    // uint64_t nhdrs = IPCBUF_XFERS;
    // uint64_t hdrsz = DADA_DEFAULT_HEADER_SIZE;


    dada_key = d_k;
    num_readers = n_r;
    nbufs = nb;
    bufsz = bs;

    if (!dada_key)
        dada_key = DADA_KEY;
    if (!num_readers)
        num_readers = NUM_READERS;
    if (!nbufs)
        nbufs = NBUFS;
    if (!bufsz)
        bufsz = BUFSIZE;

    fprintf(stderr, "Creating buffer with :\n"
            "dada_key = %x\n"
            "num_readers = %d\n"
            "nbufs = %"PRIu64"\n"
            "bufsz = %"PRIu64"\n",
            dada_key, num_readers, nbufs, bufsz);

    //Set the number of readers which can attach to the buffer
    if ((num_readers < 1) || (num_readers > 5))
    {
        fprintf (stderr, "Number of readers was not sensible: %d\n", num_readers);
        return -1;
    }

    //create the data buffer
    if (ipcbuf_create (data_block, dada_key, nbufs, bufsz, num_readers) < 0)
    {
        fprintf (stderr, "Could not create DADA data block\n");
        return -1;
    }

    fprintf (stderr, "Created DADA data block with"
             " nbufs=%"PRIu64" bufsz=%"PRIu64" nread=%d\n", nbufs, bufsz, num_readers);

    //Lock the data into RAM (stop the buffer from being paged)
    if (lock && ipcbuf_lock (data_block) < 0)
    {
        fprintf (stderr, "Could not lock DADA data block into RAM\n");
        return -1;
    }

    return 0;
}

int lock_buffer(ipcbuf_t* data_block)
{
    if (ipcbuf_lock (data_block) < 0)
    {
        fprintf (stderr, "Could not lock DADA data block into RAM\n");
        return -1;
    }
    return 0;
}

/*test code to read data from a buffer, shows a simple example of how to consume data*/
void consume (ipcbuf_t* data_block)
{
    uint64_t num_clear, num_full, num_written, written_index;
    while (1)
    {
        fprintf(stderr, "------------------READING FROM BUFFER-------------------\n");
        char *read;
        uint64_t num_read;
        if ((num_read = read_next_buf(data_block, &read)) < 0)
            fprintf (stderr, "FAILED TO READ\n");
        else if (num_read == 0)
            fprintf(stderr, "NOTHING TO READ\n");
        else
            fprintf(stderr, "READ %"PRIu64" bytes ", num_read);
        // fprintf(stderr, "output = ");
        // int i;
        // for (i = 0; i < num_read; i++)
        //     fprintf(stderr, "%"PRId8" ,", read[i]);
        // fprintf(stderr,"\n");

        hexdump(read, num_read, 16);

        num_clear = ipcbuf_get_nclear(data_block);
        num_full = ipcbuf_get_nfull(data_block);
        num_written = ipcbuf_get_write_count(data_block);
        written_index = ipcbuf_get_write_index(data_block);

        fprintf (stderr, "NUMBER OF CLEAR BUFFERS : %"PRIu64"\n", num_clear);
        fprintf (stderr, "NUMBER OF FULL BUFFERS : %"PRIu64"\n", num_full);
        fprintf (stderr, "NUMBER OF WRITTEN BUFFERS : %"PRIu64"\n", num_written);
        fprintf (stderr, "INDEX OF WRITTEN BUFFERS : %"PRIu64"\n", written_index);
        // sleep (3);
    }
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

/*test data to write data to the buffer, shows a simple example of how to place data in teh buffer*/
void produce (ipcbuf_t* data_block)
{
    uint64_t num_clear, num_full, num_written, written_index;
    uint64_t i = 0;
    int num = 0;
    char* input;
    while (1)
    {
        if (!buf)
            buf = get_next_buf(data_block);
        if ((8 * i) + 8 > bufsz)
        {
            //Mark buffer filled at last written pos
            mark_filled (data_block, 8 * i);
            buf = get_next_buf(data_block);
            sleep(5);
            i = 0;
        }
        int num = num % 256;
        input = (char *) malloc (8);
        sprintf(input, "test%03d", num);
        fprintf(stderr, "-------------------WRITING %s TO BUFFER----------------\n", input);
        if (write_to_buf(data_block, buf + (8 * i)%bufsz, input, 8) < 0)
            fprintf(stderr, "FAILED TO WRITE\n");
        else
            fprintf(stderr, "WROTE SUCCESSFULLY\n");

        num_clear = ipcbuf_get_nclear(data_block);
        num_full = ipcbuf_get_nfull(data_block);
        num_written = ipcbuf_get_write_count(data_block);
        written_index = ipcbuf_get_write_index(data_block);

        fprintf (stderr, "NUMBER OF CLEAR BUFFERS : %"PRIu64"\n", num_clear);
        fprintf (stderr, "NUMBER OF FULL BUFFERS : %"PRIu64"\n", num_full);
        fprintf (stderr, "NUMBER OF WRITTEN BUFFERS : %"PRIu64"\n", num_written);
        fprintf (stderr, "INDEX OF WRITTEN BUFFERS : %"PRIu64"\n", written_index);
        i = i + 1;
        num = num + 1;
        free(input);
    }
}

int main (int argc, char **argv)
{

    ipcbuf_t data_block = IPCBUF_INIT;
    while ((arg = getopt(argc, argv, "hdk:n:r:b:lpc")) != -1)
    {

        switch (arg)
        {
        case 'h':
            usage ();
            return 0;

        case 'd':
            destroy = 1;
            break;

        case 'k':
            if (sscanf (optarg, "%x", &dada_key) != 1)
            {
                fprintf (stderr, "dada_db: could not parse key from %s\n", optarg);
                return -1;
            }
            break;

        case 'n':
            if (sscanf (optarg, "%"PRIu64"", &nbufs) != 1)
            {
                fprintf (stderr, "dada_db: could not parse nbufs from %s\n", optarg);
                return -1;
            }
            break;

        case 'b':
            if (sscanf (optarg, "%"PRIu64"", &bufsz) != 1)
            {
                fprintf (stderr, "dada_db: could not parse bufsz from %s\n", optarg);
                return -1;
            }
            break;

        case 'r':
            if (sscanf (optarg, "%d", &num_readers) != 1)
            {
                fprintf (stderr, "dada_db: could not parse number of readers from %s\n", optarg);
                return -1;
            }
            break;

        case 'l':
            lock = 1;
            break;

        case 'c':
            consume_flag = 1;
            break;

        case 'p':
            produce_flag = 1;
            break;
        }
    }

    if (destroy)
    {
        connect_to_buffer(&data_block, dada_key);
        delete_buffer(&data_block);
        return 0;
    }


    if (consume_flag)
    {
        connect_to_buffer(&data_block, dada_key);
        consume(&data_block);
    }
    else if (produce_flag)
    {
        connect_to_buffer(&data_block, dada_key);
        produce(&data_block);
    }
    else
    {
        create_buffer(&data_block, dada_key, num_readers, nbufs, bufsz);
    }

    if (lock)
    {
        lock_buffer(&data_block);
    }
}