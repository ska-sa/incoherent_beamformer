/* dada, ipc stuff */

// #include "dada_hdu.h"
// #include "dada_def.h"
// #include "dada_pwc_main.h"

// #include "ipcio.h"
// #include "multilog.h"
// #include "ascii_header.h"
// #include "daemon.h"
// #include "futils.h"

#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>



void get_settings();

// void spead_api_destroy(struct spead_api_module_shared *s, void *data);

// void *spead_api_setup(struct spead_api_module_shared *s);

void set_bit(int* array, int pos);

int get_bit(int* array, int pos);

void write_to_order_buffer(char* heap, unsigned long long offset);

void zero_dropped_packets();

void to_dada_buffer (dada_hdu_t * hdu);

// int spead_api_callback(struct spead_api_module_shared *s, struct spead_item_group *ig, void *data);

// int spead_api_timer_callback(struct spead_api_module_shared *s, void *data);