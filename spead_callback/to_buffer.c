#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <dada_buffer.h>

#include <spead_api.h>

#define F_ID_SPEAD_ID 4096
#define TIMESTAMP_SPEAD_ID 4097
#define DATA_SPEAD_ID 4098
#define DATA_LEN 2048
#define DADA_BUF_ID 0x1234
#define DADA_NUM_READERS 1
#define DADA_NUM_BUFS 16
#define NUM_HEAPS_PER_BUFFER 100


#define TS_PER_HEAP 1
#define CHANS_PER_HEAP 1024
#define POLARIZATIONS 2
#define BYTES_PER_SAMPLE 1
#define EXPECTED_HEAP_LEN TS_PER_HEAP * CHANS_PER_HEAP * POLARIZATIONS * BYTES_PER_SAMPLE

#define DADA_BUFFER_SIZE EXPECTED_HEAP_LEN * NUM_HEAPS_PER_BUFFER

#define NUM_WORKERS 4

typedef enum { false, true } bool;

char* buffer; //local pointer to current buffer
unsigned long long heap_count;
unsigned long prior_ts; //local prior ts


struct snap_shot
{
    int master_id;
    ipcbuf_t circular_buf;
    char* buffer; //global pointer to current buffer
    unsigned long long prior_ts; //first ts for this buffer
    int buffer_full;
};

void spead_api_destroy(struct spead_api_module_shared *s, void *data)
{
    struct snap_shot *ss;

    lock_spead_api_module_shared(s);

    if ((ss = get_data_spead_api_module_shared(s)) != NULL)
    {
        delete_buffer(&ss->circular_buf);

        shared_free(ss, sizeof(struct snap_shot));

        clear_data_spead_api_module_shared(s);
#ifdef DEBUG
        fprintf(stderr, "%s: PID [%d] destroyed spead_api_shared\n", __func__, getpid());
#endif
    }
    else
    {

#ifdef DEBUG
        fprintf(stderr, "%s: PID [%d] spead_api_shared is clean\n", __func__, getpid());
#endif

    }

    printf("EXITING");
    unlock_spead_api_module_shared(s);

}

void *spead_api_setup(struct spead_api_module_shared *s)
{
    struct snap_shot *ss;

    ss = NULL;

    lock_spead_api_module_shared(s);

    if (!(ss = get_data_spead_api_module_shared(s)))
    {
        ss = shared_malloc(sizeof(struct snap_shot));
        if (ss == NULL)
        {
            unlock_spead_api_module_shared(s);
            return NULL;
        }

        create_buffer(ss->circular_buf, DADA_BUF_ID, DADA_NUM_READERS, DADA_NUM_BUFS, DADA_BUFFER_SIZE);
        ss->dada_key = DADA_BUF_ID;
        ss->buffer_full = 0;
        ss->prior_ts = 0;
        ss->master_pid = 0;
        set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
    }

    unlock_spead_api_module_shared(s);
}


int spead_api_callback(struct spead_api_module_shared *s, struct spead_item_group *ig, void *data)
{
    struct snap_shot *ss;
    struct spead_api_item *itm;
    unsigned long long ts;
    unsigned long long offset;

    ss = get_data_spead_api_module_shared(s); //Get shared data module
    itm = NULL;

    itm = get_spead_item_with_id(ig, TIMESTAMP_SPEAD_ID);

    if (itm == NULL){
    fprintf(stderr, "%s: No timestamp data found (id: 0x%x)\n", __func__, SPEAD_TS_ID);
    return -1;
    }
    // TS is 5 byte unsigned 
    ts = (int)itm->i_data[0] + (int)itm->i_data[1] * 256 + (int)itm->i_data[2] * 256 * 256 + (int)itm->i_data[3] * 256 * 256 * 256 + (int)itm->i_data[4] * 256 * 256 * 256 * 256;

    itm = get_spead_item_with_id(ig, DATA_SPEAD_ID);
    if (itm == NULL){
        fprintf(stderr, "%s: No beamformer payload data found.\n", __func__); 
        return -1;
    }
    // check that the heap size matches our expectations
    if (itm->i_data_len != EXPECTED_HEAP_LEN) {
       fprintf(stderr,"%s: Expecting heap size of %i, got %i\n",__func__,EXPECTED_HEAP_LEN, itm->i_data_len);
       return -1;
    }

    lock_spead_api_module_shared(s); //lock shared data

    if (ss == NULL)
    {
        fprintf(stderr, "ss is null");
        unlock_spead_api_module_shared(s);
        //Shared resources empty, unlock and return
        return -1;
    }

    if (ss->prior_ts == 0) { //First thread to run
        if (ss->master_pid == 0){
            ss->master_pid = getpid();
            // make sure we know who is boss
            ss->prior_ts = ts;
            ss->buffer=get_next_buf(ss->circular_buf);
            fprintf(stderr,"Reset prior_ts base to %llu. Master pid set to %d\n. Created first buffer",ts,ss->master_pid);
            set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
        }
    }

    if (buffer == NULL) //Must be first time non master thread has run
        buffer = ss->buffer;

    offset = (ts - prior_ts) * EXPECTED_HEAP_LEN;

    if (offset >= DADA_BUFFER_SIZE){ //Need to go to next buffer
        if(ss->buffer_full == 0){ //First thread to need next buffer
            fprintf(stderr,"Trigger new buffer...(%d)\n",getpid());
            ss->buffer=get_next_buf(ss->circular_buf);
            ss->prior_ts += NUM_HEAPS_PER_BUFFER;
        }
        ss->buffer_full++;
        if (ss->buffer_full == NUM_WORKERS) //Last thread to need next buffer
        {
            fprintf(stderr,"Mark old buffer as full...(%d)\n",getpid());
            mark_filled(ss->circular_buf, DADA_BUFFER_SIZE);
            ss->buffer_full = 0;
        }

        set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));

        buffer = ss->buffer;
        prior_ts = ss->prior_ts;
        offset = (ts - prior_ts) * EXPECTED_HEAP_LEN;
    }

    unlock_spead_api_module_shared(s);

    memcpy(buffer + offset, itm, EXPECTED_HEAP_LEN);  //Copy data to buffer

    return 0;
}

int spead_api_timer_callback(struct spead_api_module_shared *s, void *data)
{
    return 0;
}