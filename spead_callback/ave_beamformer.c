#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <spead_api.h>

#define TIMESTAMP_SPEAD_ID 5632
// #define DATA_SPEAD_ID 45057

// #define TIMESTAMP_SPEAD_ID 4097
#define DATA_SPEAD_ID 4098
#define N_CHANS 1024
#define TIMESTAMPS_PER_HEAP 4
#define THRESHOLD 500
#define PREFIX "/data1/times"
#define FILE_HEAP_LEN 1000000
#define AVE_OVER 1048576

typedef enum { false, true } bool;

int w_fd2;
unsigned long long heap_cnt;
int file_count;
int timestamp_id, data_id, ts_per_heap, n_chans;
char* prefix;
unsigned long long prior_ts;

float min, max, ave, threshold;

int count = 0;



struct snap_shot
{
    unsigned long long prior_ts;
    char filename[255];
    char *prefix;
    float ave[N_CHANS];
    unsigned long long offset;
    unsigned long long data_len;
    int id;
    bool with_markers;
    int num_writes;

    int master_pid;
};

void spead_api_destroy(struct spead_api_module_shared *s, void *data)
{
    struct snap_shot *ss;

    lock_spead_api_module_shared(s);

    if ((ss = get_data_spead_api_module_shared(s)) != NULL)
    {
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

    // fprintf (stderr, "Min = %f, Max = %f, ave = %f", min, max, ave/count);
    close(w_fd2);
    printf("EXITING");
    unlock_spead_api_module_shared(s);
}

void *spead_api_setup(struct spead_api_module_shared *s)
{
  fprintf (stderr, "SETUP");

    char *env_temp;

    struct snap_shot *ss;

   


    // State of stream??
    ss = NULL;

    lock_spead_api_module_shared(s);
    //lock access to shared resources??



    if (!(ss = get_data_spead_api_module_shared(s)))
    {
        // If stream state hasn't been initialised, this must be the master thread...

        ss = shared_malloc(sizeof(struct snap_shot));
        //allocate memory for shared state
        if (ss == NULL)   // if mem allocation failed??
        {
            unlock_spead_api_module_shared(s); //unloack spead resources??
            return NULL;
        }

        

        // fprintf(stderr, "CONFIGURATION\n---------------------------------\nPREFIX=%s\nFILENAME=%s\nSAVEID=%d\nWITHMARKERS?%s\n",
        //         ss->prefix, ss->filename, ss->id, ss->with_markers == true ? "TRUE" : "FALSE");

        ss->master_pid = 0;
        ss->num_writes = 0;


        set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));

    }

    unlock_spead_api_module_shared(s);
    //Unlock shared resources

    env_temp = getenv("TIMESTAMP_SPEAD_ID");
    if (env_temp != NULL) timestamp_id = atoi(env_temp);
    else timestamp_id = TIMESTAMP_SPEAD_ID;

    env_temp = getenv("DATA_SPEAD_ID");
    //Set data file prefix from environment variables, or just use default of /Data1/latest
    if (env_temp != NULL) data_id = atoi(env_temp);
    else data_id = DATA_SPEAD_ID;

    env_temp = getenv("N_CHANS");
    //Set data file prefix from environment variables, or just use default of /Data1/latest
    if (env_temp != NULL) n_chans = atoi(env_temp);
    else n_chans = N_CHANS;

    env_temp = getenv("TIMESTAMPS_PER_HEAP");
    //Set data file prefix from environment variables, or just use default of /Data1/latest
    if (env_temp != NULL) ts_per_heap = atoi(env_temp);
    else ts_per_heap = TIMESTAMPS_PER_HEAP;

    env_temp = getenv("PREFIX");
    //Set data file prefix from environment variables, or just use default of /Data1/latest
    if (env_temp != NULL) prefix = env_temp;
    else prefix = PREFIX;

    heap_cnt = 0;
    file_count = 0;

    fprintf (stderr, "CONFIGURATION\nTIMESTAMP ID = %d\nDATA ID = %d\nN_CHANS = %d\n, TIMESTAMPS PER HEAP = %d\nTHRESHOLD = %f\n", timestamp_id, data_id, n_chans, ts_per_heap, threshold);
    return NULL;
}


int spead_api_callback(struct spead_api_module_shared *s, struct spead_item_group *ig, void *data)
{
  // fprintf (stderr, "CALLBACK");
    struct snap_shot *ss;
    struct spead_api_item *itm;
    unsigned long long local_offset;
    bool release = false;

    unsigned long long ts;

    itm = NULL;

    itm = get_spead_item_with_id(ig, timestamp_id);


    if (itm == NULL)
        fprintf(stderr,"NO TS\n");

    // ts = (unsigned long long)itm->i_data[0] + (unsigned long long)itm->i_data[1] * 256 + (unsigned long long)itm->i_data[2] * 256 * 256 + (unsigned long long)itm->i_data[3] * 256 * 256 * 256 + (unsigned long long)itm->i_data[4] * 256 * 256 * 256 * 256;
    ts = (int)itm->i_data[0] + (int)itm->i_data[1] * 256 + (int)itm->i_data[2] * 256 * 256 + (int)itm->i_data[3] * 256 * 256 * 256 + (int)itm->i_data[4] * 256 * 256 * 256 * 256;

    // fprintf (stderr, "[%d] TIMESTAMP = %llu\n", getpid(), ts);
    ss = get_data_spead_api_module_shared(s);
    //Get shared data

    itm = NULL;

    lock_spead_api_module_shared(s);
    if (ss == NULL){
      unlock_spead_api_module_shared(s);
      return -1;
    }
  
    if (ss->prior_ts == 0) {
        if (ss->master_pid == 0) {
          ss->master_pid = getpid();
          ss->prior_ts=ts;
        // make sure we know who is boss

        snprintf(ss->filename,255,"%s/%llu.dat",prefix, ts); 
        fprintf(stderr,"%s: Requesting new file %s\n",__func__,ss->filename);
        set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
        }
    }

    if (prior_ts == 0 || prior_ts < ss->prior_ts)
        prior_ts = ss->prior_ts;
  unlock_spead_api_module_shared(s);

  if (w_fd2 == NULL)
    w_fd2 = open(ss->filename, O_WRONLY | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        

    itm = get_spead_item_with_id(ig, data_id);

    if (itm == NULL)
        fprintf(stderr,"NO DATA\n");

    lock_spead_api_module_shared(s);
    add (itm, ss->ave);
    set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
    unlock_spead_api_module_shared(s);

    if (ts - prior_ts >= AVE_OVER){
        lock_spead_api_module_shared(s);
        pwrite (w_fd2, ss->ave, sizeof(float) * N_CHANS, sizeof(float) * N_CHANS * ss->num_writes);
        memset(ss->ave, 0, sizeof(float) * N_CHANS);
        ss->prior_ts = ts;
        set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
        unlock_spead_api_module_shared(s);
    }
    


    // fprintf (stderr, "[%d] ts = %llu, prior_ts = %llu\n ts - prior_ts = %llu, /2048 = %llu", getpid(), ts, prior_ts, ts - prior_ts, (ts - prior_ts)/2048);
    // int pulse = check_pulse(itm->i_data, ts, (ts - prior_ts)/2048);
    // if ( pulse != -1){
    //   lock_spead_api_module_shared(s);
    //   // fprintf (stderr, "Pulse detected at timestamp %d in spectra %d. NUMPULSE = %d\n", ts, pulse, ss->num_pulse); 
    //   pwrite (w_fd2, &ts, 8, 8 * ss->num_pulse);
    //   set_data_spead_api_module_shared(s, ss, sizeof(struct snap_shot));
    //   unlock_spead_api_module_shared(s);
    // }
    return 0;
}

void add (int8_t * data, float* ave){
    int i,j, start;
    for (j = 0; j < TIMESTAMPS_PER_HEAP; j++){
        start = 1024 * j;
        for (i = 0; i < N_CHANS; i++){
            ave[i] = ave[i] + (float)data[start + i];
        }
    }
}

void hexdump(unsigned char *buffer, unsigned long long index, unsigned long long width)
{
    unsigned long i = 0;
    int j;
    fprintf(stderr,"\n%08lx\t:",i);
    if (index > width){
        
        int j;
        for (j = 0; j < index - width; j=j+width){
            for (i=j;i<j+width;i++)
            {
                fprintf(stderr,"%02x ",buffer[i]);
            }
            fprintf(stderr,"\t");
            for (i=j;i<j+width;i++)
            {
                if ('!' < buffer[i] && buffer[i] < '~')
                    fprintf(stderr,"%c",buffer[i]);
                else
                    fprintf(stderr,".");
            }
            fprintf(stderr,"\n%08lx\t:",i);
        }
        for (i;i<index; i++)
        {
            fprintf(stderr,"%02x ",buffer[i]);
        }
    }
    else{
        for (i;i<index; i++)
        {
            fprintf(stderr,"%02x ",buffer[i]);
        }
    }
    fprintf(stderr,"\n");
}

int spead_api_timer_callback(struct spead_api_module_shared *s, void *data)
{
    // struct snap_shot *ss;

    // lock_spead_api_module_shared(s);

    // ss = get_data_spead_api_module_shared(s);
    // if (ss == NULL){
    //   unlock_spead_api_module_shared(s);
    //   return -1;
    // }

    // unlock_spead_api_module_shared(s);

    return 0;
}
