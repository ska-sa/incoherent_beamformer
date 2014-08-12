#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <spead_api.h>

#define F_ID_SPEAD_ID 4096
#define TIMESTAMP_SPEAD_ID 4097
#define DATA_SPEAD_ID 4098
#define DEFAULT_PREFIX "/data1/latest"
#define DEFAULT_FILENAME "spead_out"
#define FILE_HEAP_LEN 8000

typedef enum { false, true } bool;

int w_fd2;
unsigned long long heap_cnt;
int file_count;

//FROM THE PUMA2_DMADB MAIN METHOD

/* header */
char *header = default_header;
char *header_buf = 0;
unsigned header_strlen = 0;/* the size of the header string */
uint64_t header_size = 0;/* size of the header buffer */
char writemode = 'w';

/* DADA Header plus Data Unit */
dada_hdu_t *hdu;

/* primary write client main loop  */
dada_pwc_main_t *pwcm;

dmadb_t dmadb;

time_t utc;

unsigned buffer_size = 64;

static char *buffer = 0;

/* flags */
char daemon = 0; /*daemon mode */

int verbose = 0;/* verbose mode */

int mode = 0;

//-----------------------------------------

struct snap_shot
{
    unsigned long long prior_ts;
    char filename[255];
    char *prefix;
    unsigned long long offset;
    unsigned long long data_len;
    int id;
    bool with_markers;

    int file_count;
    int master_id;
    int new_file;
};

void spead_api_destroy(struct spead_api_module_shared *s, void *data)
{

}

void *spead_api_setup(struct spead_api_module_shared *s)
{

    /* Initialize DADA/PWC structures*/
    pwcm = dada_pwc_main_create ();

    pwcm->context = &dmadb;

    pwcm->log = multilog_open ("to_buffer", daemon);

    /* set up for daemon usage */
    if (daemon)
    {
        be_a_daemon ();
        multilog_serve (pwcm->log, DADA_DEFAULT_PWC_LOG);
    }
    else
    {
        //multilog_add (pwcm->log, stderr);
        multilog_serve (pwcm->log, DADA_DEFAULT_PWC_LOG);
    }

    /* create the header/data blocks */

    hdu = dada_hdu_create (pwcm->log);

    if (dada_hdu_connect (hdu) < 0)
        return EXIT_FAILURE;

    /* make data buffers readable */

    if (dada_hdu_lock_write_spec (hdu, writemode) < 0)
        return EXIT_FAILURE;

    /* if we are a stand alone application. Don't know what this means, but it comes from the puma2 code*/

    if ( mode == 1)
    {

        header_size = ipcbuf_get_bufsz (hdu->header_block);
        multilog (pwcm->log, LOG_INFO, "header block size = %llu\n", header_size);

        header_buf = ipcbuf_get_next_write (hdu->header_block);

        if (!header_buf)
        {
            multilog (pwcm->log, LOG_ERR, "Could not get next header block\n");
            return EXIT_FAILURE;
        }

        /* if header file is presented, use it. If not set command line attributes */
        if (header_file)
        {
            if (fileread (header_file, header_buf, header_size) < 0)
            {
                multilog (pwcm->log, LOG_ERR, "Could not read header from %s\n", header_file);
                return EXIT_FAILURE;
            }
        }
        else
        {
            header_strlen = strlen(header);
            memcpy (header_buf, header, header_strlen);
            memset (header_buf + header_strlen, '\0', header_size - header_strlen);

            if (ascii_header_set (header_buf, "HDR_SIZE", "%llu", header_size) < 0)
            {
                multilog (pwcm->log, LOG_ERR, "Could not write HDR_SIZE to header\n");
                return -1;
            }

            if (verbose) fprintf(stderr, "Observation ID is %s\n", ObsId);
            if (ascii_header_set (header_buf, "OBS_ID", "%s", ObsId) < 0)
            {
                multilog (pwcm->log, LOG_ERR, "Could not write OBS_ID to header\n");
                return -1;
            }

            if (verbose) fprintf(stderr, "File size is %lu\n", fSize);
            if (ascii_header_set (header_buf, "FILE_SIZE", "%lu", fSize) < 0)
            {
                multilog (pwcm->log, LOG_ERR, "Could not write FILE_SIZE to header\n");
                return -1;
            }
        }

        /* if number of buffers is not specified, */
        /* find it from number of seconds, based on 80 MB/sec sample rate*/
        if (nbufs == 0)
        {
            float tmp;
            tmp = (float)((80000000 * nSecs) / BUFSIZE);
            nbufs = (int) tmp;
            if (verbose) fprintf(stderr, "Number of bufs is %d %f\n", nbufs, tmp);
        }

        /* write UTC_START to the header */
        if (ascii_header_set (header_buf, "UTC_START", "%s", buffer) < 0)
        {
            multilog (pwcm->log, LOG_ERR, "failed ascii_header_set UTC_START\n");
            return -1;
        }

        /* donot set header parameters anymore - acqn. doesn't start */
        if (ipcbuf_mark_filled (hdu->header_block, header_size) < 0)
        {
            multilog (pwcm->log, LOG_ERR, "Could not mark filled header block\n");
            return EXIT_FAILURE;
        }

        /* The write loop: repeat for n buffers */
        while (dmadb.buf < nbufs - 1 )
        {

            uint64_t bsize = BUFSIZE;

            src = (char *)get_next_buf(pwcm, &bsize);

            /* write data to datablock */
            if (( ipcio_write(hdu->data_block, src, BUFSIZE) ) < BUFSIZE )
            {
                multilog(pwcm->log, LOG_ERR, "Cannot write requested bytes to SHM\n");
                return EXIT_FAILURE;
            }

            wrCount = ipcbuf_get_write_count ((ipcbuf_t *)hdu->data_block);
            rdCount = ipcbuf_get_read_count ((ipcbuf_t *)hdu->data_block);
            if (verbose) fprintf(stderr, "%d %d %d\n", dmadb.buf, wrCount, rdCount);

        }

        if ( stop_acq(pwcm) != 0)
            fprintf(stderr, "Error stopping acquisition");

        if (dada_hdu_unlock_write (hdu) < 0)
            return EXIT_FAILURE;

        if (dada_hdu_disconnect (hdu) < 0)
            return EXIT_FAILURE;

    }

    return NULL;
}


int spead_api_callback(struct spead_api_module_shared *s, struct spead_item_group *ig, void *data)
{

    return 0;
}

int write_data(void *data, uint64_t data_len, unsigned long offset)
{

    return 0;
} // end of write_bf_data

int spead_api_timer_callback(struct spead_api_module_shared *s, void *data)
{
    return 0;
}