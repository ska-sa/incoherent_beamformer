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

#include <unistd.h>

//DADA stuff
#include "dada_hdu.h"
#include "dada_def.h"
#include "ascii_header.h"

#include "dada_beamform.h"

#include <pthread.h>

// #include <thread>
// #include <mutex>
// #include <condition_variable>

#include <sys/shm.h>
#include "ipcutil.h"

//data out
#include <unistd.h>
#include <string.h> //memset
#include <sys/socket.h>    //for socket of course
#include <stdlib.h> //for exit(0);
#include <errno.h> //For errno - the error number
#include <netinet/udp.h>   //Provides declarations for udp header
#include <netinet/ip.h>    //Provides declarations for ip header

#define DADA_BUF_1 0x1234
#define DADA_BUF_2 0x2345

//#define ACCUMULATE 256
#define N_CHANS 1024
#define N_POLS 2
#define TIMESTAMPS_PER_HEAP 4
#define BYTES_PER_SAMPLE 1
#define TIMESTAMP_INCREMENT 2048

#define SYNCTIME 1449493762

#define NUM_SYNC_LOOPS 2

#define ARTEMIS_IP "169.254.100.101"
#define FILE_LOC "/home/kat/data"

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define RESET "\033[0m"

/* 
    96 bit (12 bytes) pseudo header needed for udp header checksum calculation 
*/
struct pseudo_header
{
    u_int32_t source_address;
    u_int32_t dest_address;
    u_int8_t placeholder;
    u_int8_t protocol;
    u_int16_t udp_length;
};

uint64_t current_time;
uint32_t time_wraps;
uint32_t int_count; 

/*
    Generic checksum calculation function
*/
unsigned short csum(unsigned short *ptr,int nbytes) 
{
    register long sum;
    unsigned short oddbyte;
    register short answer;
 
    sum=0;
    while(nbytes>1) {
        sum+=*ptr++;
        nbytes-=2;
    }
    if(nbytes==1) {
        oddbyte=0;
        *((u_char*)&oddbyte)=*(u_char*)ptr;
        sum+=oddbyte;
    }
 
    sum = (sum>>16)+(sum & 0xffff);
    sum = sum + (sum>>16);
    answer=(short)~sum;
     
    return(answer);
}


typedef std::chrono::time_point<std::chrono::high_resolution_clock> time_point;

struct thread_data{
   int tid;
   int port;
   unsigned long long first_ts;
   int buffer_id;
   int ts_id;
   unsigned long long num_vals;
   uint64_t acc_len;
};

int master_id = 3;            //Master-thread which we synchronise to
unsigned long long tsync; //time to sync with
pthread_mutex_t tsync_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t master_id_mutex = PTHREAD_MUTEX_INITIALIZER;


void show_heap(const spead2::recv::heap &fheap)
{
    std::cout << "Received heap with CNT " << fheap.get_cnt() << '\n';
    const auto &items = fheap.get_items();
    std::cout << items.size() << " item(s)\n";
    for (const auto &item : items)
    {
        std::cout << "    ID: 0x" << std::hex << item.id << std::dec << ' ';
        std::cout << "    ID: 0x" << item.id << std::dec << ' ';
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


//Beamform two beams, with wrapping to deal with the fact that each beam will start being captured at different
//times, so the first data in each block is from a different time.
void bf_align (int16_t * beam, int16_t * align, uint64_t num_vals, int64_t pos, uint64_t align_size){
    //uint64_t beam_size = num_vals * sizeof(uint16_t);
    //fprintf (stderr, KRED "pos = %llu\n" RESET , pos);
    if (pos + num_vals < align_size){
        //fprintf(stderr, "bf_align_1\n");
        beamform (beam, align + pos, align + pos, num_vals);
    }
    else{
        //fprintf(stderr, "bf_align_2\n");
        uint64_t wrap = (align_size - pos);
        beamform (beam, align + pos, align + pos, wrap);
        //fprintf(stderr, "bf_align_wrap\n");
        beamform (beam + wrap, align, align, num_vals - wrap);
    }
    // fprintf (stderr, "out_align\n");
}

//Capture data from the incoming spead stream from the 2 beam beamformers
void capture_spead(void* threadarg)
{
    fprintf (stderr, KYEL "IN THREAD\n" RESET);
    struct thread_data *my_data;
    my_data = (struct thread_data *) threadarg;
    int port = my_data->port;
    unsigned long long first_ts = my_data->first_ts;
    unsigned long long num_vals = my_data->num_vals;
    int buffer_id = my_data->buffer_id;
    int ts_id = my_data->ts_id;
    int tid = my_data->tid;
    uint64_t acc_len = my_data->acc_len;

    int data_id, timestamp_id;

    int16_t* order_buffer;
    unsigned long long ts2;
    long long tsdiff2;
    unsigned long long ts;
    uint64_t out_buffer_size = sizeof(uint16_t) * acc_len * 4 * 10;

    spead2::thread_pool worker;
    // std::shared_ptr<spead2::memory_pool> pool = std::make_shared<spead2::memory_pool>(16384, 26214400, 12, 8);

    spead2::recv::ring_stream<spead2::ringbuffer_semaphore<spead2::recv::live_heap> > stream1(worker, 62);
    boost::asio::ip::udp::endpoint endpoint1(boost::asio::ip::address_v4::any(), port);

    stream1.emplace_reader<spead2::recv::udp_reader>(
        endpoint1, spead2::recv::udp_reader::default_max_size, 512 * 1024 * 1024);

    fprintf (stderr, KYEL "ATTACHING\n" RESET);

    order_buffer = (int16_t *)shmat(buffer_id, NULL, 0);
    // ts = *((unsigned long long *)shmat(ts_id, NULL, 0));

    fprintf (stderr, KYEL "ATTACHED\n" RESET);

    std::vector<spead2::recv::item> items1;
    //fprintf(stderr, "{%d} before recieve item\n", tid);
    spead2::recv::heap fh1 = stream1.pop();
    //fprintf(stderr, "{%d} before get item\n", tid);
    // show_heap(fh1);
    //items1 = fh1.get_items();
    // prev2 = ts2;
    //ts2 = *((unsigned long long *)items1[1].ptr);
    //fprintf(stderr, "[%d] after get ts2", tid);
    //tsdiff2 = ts2 - first_ts;

    //if (tsdiff2 > 0){
        //int64_t pos2 = ((tsdiff2) * num_vals / 536870912) % out_buffer_size / 2;
        //bf_align ((uint16_t *) items1[1].ptr, order_buffer, num_vals, pos2, out_buffer_size / 2);
    //}

    fprintf(stderr, "[%d] tsync = %llu\n", tid, tsync);

    for (int i = 0; i < NUM_SYNC_LOOPS; i++){
        //if (ts2 < tsync && ts2 != 0){
        //    pthread_mutex_lock(&tsync_mutex);
        //    pthread_mutex_lock(&master_id_mutex);
        //    tsync = ts2;
        //    master_id = tid;
        //    pthread_mutex_unlock(&tsync_mutex);
        //   pthread_mutex_unlock(&master_id_mutex);
        //    fprintf(stderr, "[%d] new tsync = %llu\n", tid, tsync);
        //}
        fh1 = stream1.pop();
        items1 = fh1.get_items();
        for (int j = 0; j < 4; j++){
          //fprintf (stderr, "[%d, %d] id = %d\n",tid, j, items1[j].id);
          if (items1[j].id == 4096)
            timestamp_id = j;
          else if (items1[j].id == 8193)
            data_id = j;
        }
        std::vector<spead2::descriptor> desc = fh1.get_descriptors();
        // prev2 = ts2;
        ts2 = *((unsigned long long *)items1[timestamp_id].ptr);
        //show_heap(fh1);
        fprintf (stderr, "[%d] ts2 = %llu\n", tid, ts2);
        fprintf (stderr, "[%d] *((unsigned long long *)items1[timestamp_id].ptr = %llu\n", tid, *((unsigned long long *)items1[timestamp_id].ptr));
        //fprintf (stderr, "[%d] name = %s\n", tid, desc[1].name.c_str());
        tsdiff2 = ts2 - first_ts;
        if (ts2 < tsync && ts2 != 0){
            pthread_mutex_lock(&tsync_mutex);
            pthread_mutex_lock(&master_id_mutex);
            tsync = ts2;
            master_id = tid;
            pthread_mutex_unlock(&tsync_mutex);
            pthread_mutex_unlock(&master_id_mutex);
            fprintf(stderr, "[%d] new tsync = %llu\n", tid, tsync);
        }
    }

    fprintf (stderr, "[%d] timestamp_id = %d\n",tid, timestamp_id);
    fprintf (stderr, "[%d] data_id = %d\n",tid,data_id);

    if (master_id == tid){
         pthread_mutex_lock(&tsync_mutex);
         tsync = ts2;
         pthread_mutex_unlock(&tsync_mutex);
    }
    int64_t pos2;

    while (true)
    {
        try{
            
            pthread_mutex_lock(&tsync_mutex);
            ts = tsync;
            pthread_mutex_unlock(&tsync_mutex);

            int64_t diff = ts - ts2;

            // fprintf (stderr, "[%d] ts = %llu\n", tid, ts);
            // fprintf (stderr, "[%d] ts2 = %llu\n", tid, ts2);
            // fprintf (stderr, "[%d] diff = %lld\n", tid, diff);

            if (diff > 0 || master_id == tid){
                pos2 = ((tsdiff2) * num_vals / 536870912) % (out_buffer_size/2);
                fprintf (stderr, KRED "[%d] pos = %llu\n" RESET ,tid, pos2);
                bf_align ((int16_t *) items1[data_id].ptr, order_buffer, num_vals, pos2, out_buffer_size/2);
                fh1 = stream1.pop();
                items1 = fh1.get_items();
                for (int j = 0; j < 4; j++){
                  //fprintf (stderr, "[%d, %d] id = %d\n",tid, j, items1[j].id);
                  if (items1[j].id == 4096)
                    timestamp_id = j;
                  else if (items1[j].id == 8193)
                    data_id = j;
                }
                // prev2 = ts2;
                ts2 = *((unsigned long long *)items1[timestamp_id].ptr);
                tsdiff2 = ts2 - first_ts;
                diff = ts - ts2;
            }
            else
                sleep(1);

            // if (diff > 0 || ts == 0){
                //fprintf (stderr, KYEL "[%d] second\n" RESET, tid, (diff)/536870912);
                pos2 = ((tsdiff2) * num_vals / 536870912) % (out_buffer_size/2);
                fprintf (stderr, KRED "[%d] pos = %llu\n" RESET ,tid, pos2);
                bf_align ((int16_t *) items1[data_id].ptr, order_buffer, num_vals, pos2, out_buffer_size/2);
                // sync[1][pos2/67108864/sizeof(uint16_t)] = 1;
                fh1 = stream1.pop();
                items1 = fh1.get_items();
                for (int j = 0; j < 4; j++){
                  //fprintf (stderr, "[%d, %d] id = %d\n",tid, j, items1[j].id);
                  if (items1[j].id == 4096)
                    timestamp_id = j;
                  else if (items1[j].id == 8193)
                    data_id = j;
                }
                // prev2 = ts2;
                ts2 = *((unsigned long long *)items1[timestamp_id].ptr);
                tsdiff2 = ts2 - first_ts;

            // }
            // else{
            //     if ((diff)/536870912 < -1)
            //         fprintf(stderr, "[%d] - ts = %llu, ts2 = %llu\n", tid, ts, ts2);
            // }

            // fprintf (stderr, KRED "[%d] diff2 : %lld\n" RESET, tid, (tsdiff2)/536870912);
            //fprintf (stderr, KRED "[%d] diff : %lld ts = %llu, ts2 = %llu\n" RESET, tid, (diff)/536870912, ts, ts2);

            if (master_id == tid){
                pthread_mutex_lock(&tsync_mutex);
                tsync = ts2;
                pthread_mutex_unlock(&tsync_mutex);
            }
        }
        catch (spead2::ringbuffer_stopped &e)
        {
            break;
        }
    }

}


//Send the beamformed data via UDP
int send_udp (int16_t * in_data, uint64_t size){
   //fprintf(stderr,"sending\n"); 

     //UDP out
    int s = socket (AF_INET, SOCK_RAW, IPPROTO_RAW);
    uint32_t int_per_sec = 390625;
    if(s == -1)
    {
        fprintf(stderr, "FAILED TO CREATE SOCKET\n");
        //socket creation failed, may be because of non-root privileges
        perror("Failed to create raw socket");
        exit(1);
    }

    if (size % 8192 != 0)
    {
        fprintf(stderr, "DATA NOT DIVISIBLE\n");
        perror("Incoming data not divisible by 8192");
        //exit(1);
    }
    //fprintf(stderr, "BEFORE SEND LOOP\n");
    uint64_t i;
    for (i = 0; i < size; i=i+4096){
    //Datagram to represent the packet
    char datagram[sizeof(struct iphdr) + sizeof(struct udphdr) + 8208] , source_ip[32] , *data , *pseudogram;
    //fprintf(stderr, "datagram made\n"); 
    //zero out the packet buffer
    memset (datagram, 0, 8208);
    //fprintf(stderr, "memset\n"); 
    //IP header
    struct iphdr *iph = (struct iphdr *) datagram;
     
    //UDP header
    struct udphdr *udph = (struct udphdr *) (datagram + sizeof (struct ip));
    struct sockaddr_in sin;
    struct pseudo_header psh;
    //fprintf(stderr, "before data pointer\n");
     
    //Data part
    data = datagram + sizeof(struct iphdr) + sizeof(struct udphdr);
    //strcpy(data , "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    memcpy(data, &current_time, sizeof(uint64_t));
    memcpy(data + sizeof(uint64_t), &int_count, sizeof(uint32_t));
    uint32_t accumulate = ACCUMULATE;
    memcpy(data + sizeof(uint64_t) + sizeof(uint32_t), &accumulate, sizeof(uint32_t));
    //fprintf(stderr, "data copy i = %llu\n", i);
    memcpy(data + 16, in_data + i, 8192);

    //fprintf(stderr, "after data copy \n");
     
    //some address resolution
    strcpy(source_ip , "169.254.100.100");
     
    sin.sin_family = AF_INET;
   // fprintf(stderr, "bla\n");
    sin.sin_port = htons(8080);
    sin.sin_addr.s_addr = inet_addr (ARTEMIS_IP);
   // fprintf(stderr, "SIN BABY\n");
    //Fill in the IP Header
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = sizeof (struct iphdr) + sizeof (struct udphdr) + 8208;
    iph->id = htonl (54321); //Id of this packet
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = IPPROTO_UDP;
    iph->check = 0;      //Set to 0 before calculating checksum
    iph->saddr = inet_addr ( source_ip );    //Spoof the source ip address
    iph->daddr = sin.sin_addr.s_addr;
     
    //Ip checksum
    iph->check = csum ((unsigned short *) datagram, iph->tot_len);
     
    //UDP header
    udph->source = htons (6666);
    udph->dest = htons (8622);
    udph->len = htons(8 + 8208); //tcp header size
    udph->check = 0; //leave checksum 0 now, filled later by pseudo header
     
    //Now the UDP checksum using the pseudo header
    psh.source_address = inet_addr( source_ip );
    psh.dest_address = sin.sin_addr.s_addr;
    psh.placeholder = 0;
    psh.protocol = IPPROTO_UDP;
    psh.udp_length = htons(sizeof(struct udphdr) + 8208 );
   // fprintf(stderr, "psizin\n"); 
    int psize = sizeof(struct pseudo_header) + sizeof(struct udphdr) + 8208;
   //fprintf(stderr, "MALLOC!\n");
    pseudogram = malloc(psize);
   // fprintf(stderr, "before memcpy\n"); 
    memcpy(pseudogram , (char*) &psh , sizeof (struct pseudo_header));
    memcpy(pseudogram + sizeof(struct pseudo_header) , udph , sizeof(struct udphdr) + 8208);
    //fprintf(stderr, "before checksm\n"); 
    udph->check = csum( (unsigned short*) pseudogram , psize);
  //  fprintf(stderr, "after checksm\n");
    //loop if you want to flood :)
    //while (1)
    {
        //Send the packet
        if (sendto (s, datagram, iph->tot_len ,  0, (struct sockaddr *) &sin, sizeof (sin)) < 0)
        {
            fprintf(stderr, "sendto failed\n");
            perror("sendto failed");
        }
        //Data send successfully
        else
        {
            
            usleep(600);
            //fprintf (stderr, "Packet Send. Length : %d \n" , iph->tot_len);
        }
    }

    int_count=int_count+ACCUMULATE;
    if (int_count > int_per_sec){
        current_time++;
        int_count=int_count % int_per_sec;
	fprintf (stderr, "\ncurrent_time = %llu\n",current_time);
    }
    //fprintf (stderr, "\nint_count = %llu\n",int_count);
    //fprintf(stderr, "end loop\n");
    free(pseudogram); 
    //delete [] datagram;   
    }
    fprintf (stderr, "\nint_count = %llu\n",int_count);
//    free(pseudogram);
    //s.close();
    shutdown(s,2);
    //fprintf(stderr,"exit sending\n");
    return 0;
}

//Main thread, capture F-engine data and accumulate, start the threads to capture spead streams form 2 beam beamformers
//Send data out via UDP
void run (int port1, int port2, int port3, dada_hdu_t * hdu)
{
    int acc_count = 0;


    pthread_t threads[3];
    struct thread_data thread_data_array[3];

    unsigned long long first_ts, ts2;
    // unsigned long long * ts;
    uint64_t heap_size = N_POLS * N_CHANS * BYTES_PER_SAMPLE * TIMESTAMPS_PER_HEAP;
    fprintf(stderr, KYEL "heap_size = %llu\n" RESET, heap_size);
    fprintf(stderr, KYEL "vals_per_heap = %llu\n" RESET, heap_size / BYTES_PER_SAMPLE);
    int out_file = 0;
    char filename[255];
    uint64_t count = 0;
    unsigned long long read_head = 0;
    //unsigned long long acc_len = hdu->data_block->curbufsz * 2 / ACCUMULATE;
    //uint64_t out_buffer_size = sizeof(int16_t) * 67108864 * 10;
    //fprintf (stderr, "out_buffer_size = %llu\n", out_buffer_size);
    //int ob_id, ts_id;
    //int16_t * out;
    //out = ipc_alloc("6543", out_buffer_size, IPC_CREAT | IPC_EXCL | 0666, &ob_id);
    // ts = ipc_alloc("5432", sizeof(unsigned long long), IPC_CREAT | IPC_EXCL | 0666, &ts_id);
    //memset(out,0,out_buffer_size);
    
    fprintf (stderr, "after memset\n");
    if (dada_hdu_lock_read (hdu) < 0){
         fprintf(stderr, KRED "hdu CONNECT FAILED\n" RESET);
         // return EXIT_FAILURE;
    }

    initial_header(hdu);
    
    if (ipcio_is_open (hdu->data_block)){
        fprintf (stderr, KGRN "OPEN\n" RESET);
    }

    // prev1 = ts1;
    first_ts = get_timestamp(hdu);
    if (first_ts == 0){
        fprintf (stderr, "first_ts = 0\n");
        first_ts = get_timestamp(hdu);
    }

    ts2 = first_ts;
    pthread_mutex_lock(&tsync_mutex);
    tsync = first_ts; //Initial sync ts
    pthread_mutex_unlock(&tsync_mutex); 
    fprintf(stderr,"[3] tsync = %llu\n", tsync);
    unsigned long long num_vals, pos1, blockid;
    char * buffer;

    buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);
    fprintf(stderr, KYEL "dada_block_size = %llu\n" RESET, hdu->data_block->curbufsz);
    unsigned long long acc_len = hdu->data_block->curbufsz * 2 / ACCUMULATE;
    fprintf(stderr, KGRN "acc_len = %llu\n" RESET, acc_len);
    int16_t * accumulated = (int16_t *)malloc(sizeof(int16_t) * acc_len);
    

    num_vals = accumulate (hdu->data_block->curbuf, accumulated, hdu->data_block->curbufsz);
    //bf_align (accumulated, out, num_vals, pos1, out_buffer_size / 2);
    // sync[0][pos1/67108864/sizeof(int16_t)] = 1;
    uint64_t acc_size = hdu->data_block->curbufsz/ACCUMULATE;
    fprintf (stderr, "acc_size = %llu\n", acc_size);   
    uint64_t out_buffer_size = sizeof(int16_t) *  acc_len * 4 * 10;
    fprintf (stderr, "out_buffer_size = %llu\n", out_buffer_size);
    int ob_id, ts_id;
    int16_t * out;
    out = ipc_alloc("6543", out_buffer_size, IPC_CREAT | IPC_EXCL | 0666, &ob_id);
    // ts = ipc_alloc("5432", sizeof(unsigned long long), IPC_CREAT | IPC_EXCL | 0666, &ts_id);
    memset(out,0,out_buffer_size);


    // ssize_t size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);
    ssize_t size;
    //free (accumulated);

    thread_data_array[0].tid = 0;
    thread_data_array[0].port = 7161;
    thread_data_array[0].first_ts = first_ts;
    thread_data_array[0].buffer_id = ob_id;
    thread_data_array[0].ts_id = ts_id;
    thread_data_array[0].num_vals = num_vals;
    thread_data_array[0].acc_len = acc_len;
    fprintf (stderr, KGRN "CREATE THREAD\n" RESET);

    thread_data_array[1].tid = 1;
    thread_data_array[1].port = 7162;
    thread_data_array[1].first_ts = first_ts;
    thread_data_array[1].buffer_id = ob_id;
    thread_data_array[1].ts_id = ts_id;
    thread_data_array[1].num_vals = num_vals;
    thread_data_array[1].acc_len = acc_len;

    thread_data_array[2].tid = 2;
    thread_data_array[2].port = 7163;
    thread_data_array[2].first_ts = first_ts;
    thread_data_array[2].buffer_id = ob_id;
    thread_data_array[2].ts_id = ts_id;
    thread_data_array[2].num_vals = num_vals;
    thread_data_array[2].acc_len = acc_len;

    //pthread_create(&threads[0], NULL, capture_spead, &thread_data_array[0]);
    //pthread_create(&threads[1], NULL, capture_spead, &thread_data_array[1]);
    //pthread_create(&threads[2], NULL, capture_spead, &thread_data_array[2]);

    int64_t tsdiff2;

    for (int i = 0; i < NUM_SYNC_LOOPS; i++){
        if (ts2 < tsync){
            pthread_mutex_lock(&tsync_mutex);
            pthread_mutex_lock(&master_id_mutex);
            tsync = ts2;
            master_id = 3;
            pthread_mutex_unlock(&tsync_mutex);
            pthread_mutex_unlock(&master_id_mutex);
            fprintf(stderr, "[%d] new tsync = %llu\n", 3, tsync);
        }
        size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);

        // free(accumulated);
        // prev2 = ts2;
        ts2 = get_timestamp(hdu);
        tsdiff2 = ts2 - first_ts;

        buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);
    }

    snprintf(filename,255,"%s/%llu.dat",FILE_LOC, first_ts);

    out_file = open(filename, O_WRONLY | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    fprintf (stderr, "opened file %s", filename);


    while (true)
    {

        pthread_mutex_lock(&tsync_mutex);
        unsigned long long ts = tsync;
        pthread_mutex_unlock(&tsync_mutex);

        //fprintf (stderr, KGRN "GRABBED\n" RESET);
        int64_t diff = ts - ts2;
        //accumulated = (int16_t *)malloc(sizeof(int16_t) * 67108864);
        //fprintf(stderr, KYEL "timestamp = %llu\n" RESET, ts2);
        if (diff > 0 || master_id == 3){
            //fprintf(stderr, "diff = %lld\n", diff);
            
            int64_t pos1 = ((tsdiff2) * acc_len / 536870912) % (out_buffer_size/2);
            fprintf (stderr, KRED "[%d] pos = %llu\n" RESET ,3, pos1);

            num_vals = accumulate (hdu->data_block->curbuf, accumulated, hdu->data_block->curbufsz);
            bf_align (accumulated, out, num_vals, pos1, out_buffer_size/2);
            // sync[0][pos1/67108864/sizeof(int16_t)] = 1;
           // fprintf(stderr, KYEL "curbufsz = %llu\n" RESET, hdu->data_block->curbufsz);
            size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);

            // free(accumulated);

            ts2 = get_timestamp(hdu);
            tsdiff2 = ts2 - first_ts;

            buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);
            diff = ts - ts2;
            count++;
        }
        else
            sleep(1);

        if (diff > 0){
            //fprintf(stderr, KYEL "2timestamp = %llu\n" RESET, ts2);
            //fprintf(stderr, "diff2 = %lld\n", diff);

            //accumulated = (int16_t *)malloc(sizeof(int16_t) * acc_len);
            int64_t pos1 = ((tsdiff2) * acc_len / 536870912) % (out_buffer_size/2);
            fprintf (stderr, KRED "[%d] pos = %llu\n" RESET ,3, pos1);
            num_vals = accumulate (hdu->data_block->curbuf, accumulated, hdu->data_block->curbufsz);
            bf_align (accumulated, out, num_vals, pos1, out_buffer_size/2);
            // sync[0][pos1/67108864/sizeof(int16_t)] = 1;
            size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);

            ts2 = get_timestamp(hdu);
            tsdiff2 = ts2 - first_ts;


            buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);
            count++;

            
        }

        //fprintf (stderr, KRED "[%d] diff : %lld\n" RESET, 3, (diff)/536870912);

	
        if (master_id == 3){
            pthread_mutex_lock(&tsync_mutex);
            tsync = ts2;
            pthread_mutex_unlock(&tsync_mutex);
        }
        //fprintf(stderr, KYEL "num_vals = %llu\n" RESET, num_vals);
        //fprintf(stderr, KYEL "tsdiff2= %llu\n" RESET, tsdiff2);
        //fprintf(stderr, KYEL "diff= %lld\n" RESET, diff);
       
	uint32_t wrapped = 0;

        if (ts < first_ts && wrapped == 0){
            time_wraps++;
            wrapped = 1;
        }
        else if (ts - first_ts > 536870912 * 4 && wrapped == 1)
            wrapped = 0;

        if (tsdiff2 * acc_len / 536870912 - read_head > num_vals)
        {
            //int seconds = (int)((float)ts/12207.03125);
            uint64_t seconds = time_wraps * 1099511627776 + ts * 0.000000005;
            //fprintf (stderr, "\nts = %llu, ts/12207.03125 = %llu\n",ts, seconds);
            current_time = SYNCTIME + seconds;
            int_count = ts%200000000/512;
            fprintf (stderr, "\n[o]int_count = %llu\n",int_count);
            //fprintf (stderr, "read_head = %llu\n", read_head);
            // fprintf (stderr, "tsdiff2 = %lld\n", tsdiff2);
            // fprintf (stderr, "(tsdiff2 * 67108864 / 536870912 - read_head) = %lld, num_vals * sizeof(int16_t) * 2 = %d\n", (tsdiff2 * 67108864 / 536870912 - read_head), num_vals * sizeof(int16_t) * 2);
            int64_t size = (num_vals);
            pwrite(out_file, out + read_head % (out_buffer_size/2), size, read_head*2);
            // pwrite(out_file, accumulated, sizeof(int16_t) * 67108864, read_head);
            // pwrite(out_file, buffer, sizeof(int16_t) * 67108864, read_head);
            fprintf (stderr, KRED "read_head %% out_buffer_size = %llu\n" RESET ,  read_head % (out_buffer_size/2));
            send_udp(out + read_head % (out_buffer_size/2) , size);
            //fprintf (stderr, "read_head = %llu, size = %llu", read_head, size);
            memset(out + read_head % (out_buffer_size/2), 0, size);
            read_head = read_head + num_vals;
            // read_head = read_head + size;
            // read_head = read_head + sizeof(int16_t) * 67108864;
        }
       //free(accumulated);
       }
     free(accumulated);
     //fprintf(stderr, "EXITING\n");
}

//Not used anymore, none threaded version
static void run_ringbuffered(int port1, int port2, int port3, dada_hdu_t * hdu)
{
    spead2::thread_pool worker;
    std::shared_ptr<spead2::memory_pool> pool = std::make_shared<spead2::memory_pool>(16384, 26214400, 12, 8);

    spead2::recv::ring_stream<spead2::ringbuffer_semaphore<spead2::recv::live_heap> > stream1(worker, 1024);
    spead2::recv::ring_stream<spead2::ringbuffer_semaphore<spead2::recv::live_heap> > stream2(worker, 1024);
    spead2::recv::ring_stream<spead2::ringbuffer_semaphore<spead2::recv::live_heap> > stream3(worker, 1024);

    stream1.set_memory_pool(pool);
    stream2.set_memory_pool(pool);
    stream3.set_memory_pool(pool);

    boost::asio::ip::udp::endpoint endpoint1(boost::asio::ip::address_v4::any(), 7161);
    boost::asio::ip::udp::endpoint endpoint2(boost::asio::ip::address_v4::any(), 7162);
    boost::asio::ip::udp::endpoint endpoint3(boost::asio::ip::address_v4::any(), 7163);

    stream1.emplace_reader<spead2::recv::udp_reader>(
        endpoint1, spead2::recv::udp_reader::default_max_size, 256 * 1024 * 1024);
    stream2.emplace_reader<spead2::recv::udp_reader>(
        endpoint2, spead2::recv::udp_reader::default_max_size, 256 * 1024 * 1024);
    stream3.emplace_reader<spead2::recv::udp_reader>(
        endpoint3, spead2::recv::udp_reader::default_max_size, 256 * 1024 * 1024);

    unsigned long long first_ts, ts1, ts2, ts3, ts4;
    long long tsdiff2 = 1, tsdiff3 = 1, tsdiff4 = 1, prev1, prev2, prev3, prev4;

    uint64_t heap_size = N_POLS * N_CHANS * BYTES_PER_SAMPLE * TIMESTAMPS_PER_HEAP;
    // uint64_t buffer_allignment = 13; //Never going to be 13, could be 0

        if (dada_hdu_lock_read (hdu) < 0){
         fprintf(stderr, KRED "hdu CONNECT FAILED\n" RESET);
         // return EXIT_FAILURE;
    }

    initial_header(hdu);
    
    if (ipcio_is_open (hdu->data_block)){
        fprintf (stderr, KGRN "OPEN\n" RESET);
    }

    // prev1 = ts1;
    first_ts = get_timestamp(hdu);
    if (first_ts == 0){
        fprintf (stderr, "first_ts = 0");
        first_ts = get_timestamp(hdu);
    }

    ts1 = first_ts;

    int16_t * out;
    int sync [4][10] = {0};
    uint64_t out_buffer_size = sizeof(int16_t) * 67108864 * 10;
    fprintf (stderr, "out_buffer_size = %llu\n", out_buffer_size);
    out = (int16_t *)malloc(out_buffer_size);
    memset(out,0,out_buffer_size);
    std::vector<spead2::recv::item> items1, items2, items3;

    spead2::recv::heap fh1 = stream1.pop();
    items1 = fh1.get_items();
    // prev2 = ts2;
    ts2 = *((unsigned long long *)items1[0].ptr);
    tsdiff2 = ts2 < first_ts? first_ts - ts2 : - static_cast< long long >( ts2 - first_ts );
    fprintf (stderr, KGRN "ts2 = %llu\n" RESET, ts2);
    fprintf (stderr, KGRN "tsdiff2 = %lld\n" RESET, tsdiff2);

    spead2::recv::heap fh2 = stream2.pop();
    items2 = fh2.get_items();
    // prev2 = ts2;
    ts3 = *((unsigned long long *)items2[0].ptr);
    tsdiff3 = ts3 < first_ts? first_ts - ts3 : - static_cast< long long >( ts3 - first_ts );

    spead2::recv::heap fh3 = stream3.pop();
    items3 = fh3.get_items();
    // prev2 = ts2;
    ts4 = *((unsigned long long *)items3[0].ptr);
    tsdiff4 = ts4 < first_ts? first_ts - ts4 : - static_cast< long long >( ts4 - first_ts );
    
    //uint32_t wrapped = 0;

    //if (ts < first_ts && wrapped == 0){
//	time_wraps++;
    //    wrapped = 1;
  //  }
    //else if (ts - first_ts > 536870912 * 4 && wrapped == 1)
      //  wrapped = 0;

    while (true)
    {
        try
        {
            uint64_t pos1 = ((ts1 - first_ts) * 67108864 / 536870912) % out_buffer_size / sizeof(int16_t);
            // memset(out + pos1 ,0 ,67108864 * sizeof(uint16_t));
            uint16_t * accumulated, * beamformed;
            uint64_t blockid, num_vals;
            char * buffer;

            buffer = ipcio_open_block_read(hdu->data_block, &(hdu->data_block->curbufsz), &blockid);

            accumulated = (uint16_t *)malloc(sizeof(uint16_t) * 67108864);
            
            num_vals = accumulate (hdu->data_block->curbuf, accumulated, hdu->data_block->curbufsz);
            bf_align (accumulated, out, num_vals, pos1, out_buffer_size / 2);
            sync[0][pos1/67108864/sizeof(uint16_t)] = 1;

            fprintf (stderr, KGRN "ts1 = %llu\n" RESET, ts1);
            fprintf (stderr, KGRN "pos1 = %llu\n" RESET, pos1);

            if (tsdiff2 > 0){
                uint64_t pos2 = ((tsdiff2) * 67108864 / 536870912) % out_buffer_size / 2;
                bf_align ((uint16_t *) items1[1].ptr, out, num_vals, pos2, out_buffer_size / 2);
                sync[1][pos2/67108864/sizeof(uint16_t)] = 1;
            }

            if (tsdiff3 > 0){
                uint64_t pos3 = ((tsdiff3) * 67108864 / 536870912) % out_buffer_size / 2;
                bf_align ((uint16_t *) items2[1].ptr, out, num_vals, pos3, out_buffer_size / 2);
                sync[2][pos3/67108864/sizeof(uint16_t)] = 1;
            }

            if (tsdiff4 > 0){
                uint64_t pos4 = ((tsdiff4) * 67108864 / 536870912) % out_buffer_size / 2;
                bf_align ((uint16_t *) items3[1].ptr, out, num_vals, pos4, out_buffer_size / 2);
                sync[3][pos4/67108864/sizeof(uint16_t)] = 1;
            }

            int64_t diff = ts2 < ts1? ts1 - ts2 : - static_cast< long long >( ts2 - ts1 );

            if (diff > 0){
                fh1 = stream1.pop();
                items1 = fh1.get_items();
                // prev2 = ts2;
                ts2 = *((unsigned long long *)items1[0].ptr);
                tsdiff2 = ts2 < first_ts? first_ts - ts2 : - static_cast< long long >( ts2 - first_ts );
                diff = ts2 < ts1? ts1 - ts2 : - static_cast< long long >( ts2 - ts1 );
            }

            if (diff > 0){
                uint64_t pos2 = ((tsdiff2) * 67108864 / 536870912) % out_buffer_size/2;
                bf_align ((uint16_t *) items1[1].ptr, out, num_vals, pos2, out_buffer_size/2);
                sync[1][pos2/67108864/sizeof(uint16_t)] = 1;
                fh1 = stream1.pop();
                items1 = fh1.get_items();
                // prev2 = ts2;
                ts2 = *((unsigned long long *)items1[0].ptr);
                tsdiff2 = ts2 < first_ts? first_ts - ts2 : - static_cast< long long >( ts2 - first_ts );
            }



            // fprintf (stderr, KRED "diff1 : %llu\n" RESET, (tsdiff1)/536870912);
            fprintf (stderr, KRED "diff2 : %lld\n" RESET, (tsdiff2)/536870912);
            fprintf (stderr, KRED "diff : %lld\n" RESET, (diff)/536870912);

            diff = ts3 < ts1? ts1 - ts3 : - static_cast< long long >( ts3 - ts1 );

            if (diff > 0){
                fh2 = stream2.pop();
                items2 = fh2.get_items();
                // prev2 = ts2;
                ts3 = *((unsigned long long *)items2[0].ptr);
                tsdiff3 = ts3 < first_ts? first_ts - ts3 : - static_cast< long long >( ts3 - first_ts );
                diff = ts3 < ts1? ts1 - ts3 : - static_cast< long long >( ts3 - ts1 );
            }

            if (diff > 0){
                uint64_t pos3 = ((tsdiff3) * 67108864 / 536870912) % out_buffer_size/2;
                bf_align ((uint16_t *) items2[1].ptr, out, num_vals, pos3, out_buffer_size/2);
                sync[2][pos3/67108864/sizeof(uint16_t)] = 1;
                fh2 = stream2.pop();
                items2 = fh2.get_items();
                // prev2 = ts2;
                ts3 = *((unsigned long long *)items2[0].ptr);
                tsdiff3 = ts3 < first_ts? first_ts - ts3 : - static_cast< long long >( ts3 - first_ts );
            }

            fprintf (stderr, KRED "diff3 : %lld\n" RESET, (tsdiff3)/536870912);
            fprintf (stderr, KRED "diff : %lld\n" RESET, (diff)/536870912);


            diff = ts4 < ts1? ts1 - ts4 : - static_cast< long long >( ts4 - ts1 );

            if (diff > 0){
                fh3 = stream3.pop();
                items3 = fh3.get_items();
                // prev2 = ts2;
                ts4 = *((unsigned long long *)items3[0].ptr);
                tsdiff4 = ts4 < first_ts? first_ts - ts4 : - static_cast< long long >( ts4 - first_ts );
                diff = ts4 < ts1? ts1 - ts4 : - static_cast< long long >( ts4 - ts1 );
            }

            if (diff > 0){
                uint64_t pos4 = ((tsdiff4) * 67108864 / 536870912) % out_buffer_size/2;
                bf_align ((uint16_t *) items3[1].ptr, out, num_vals, pos4, out_buffer_size/2);
                sync[3][pos4/67108864/sizeof(uint16_t)] = 1;
                fh3 = stream3.pop();
                items3 = fh3.get_items();
                // prev2 = ts2;
                ts4 = *((unsigned long long *)items3[0].ptr);
                tsdiff4 = ts4 < first_ts? first_ts - ts4 : - static_cast< long long >( ts4 - first_ts );
            }

            fprintf (stderr, KRED "diff4 : %lld\n" RESET, (tsdiff4)/536870912);
            fprintf (stderr, KRED "diff : %lld\n" RESET, (diff)/536870912);
            
            ssize_t size =  ipcio_close_block_read(hdu->data_block, hdu->data_block->curbufsz);
            
            ts1 = get_timestamp(hdu);

            free(accumulated);
            free(beamformed);

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
    run (7160, 7161, 7162, hdu);
}
