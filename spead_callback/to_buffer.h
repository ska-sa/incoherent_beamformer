/* dada, ipc stuff */

#include "dada_hdu.h"
#include "dada_def.h"
#include "dada_pwc_main.h"

#include "ipcio.h"
#include "multilog.h"
#include "ascii_header.h"
#include "daemon.h"
#include "futils.h"

#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

/* EDT recommends datarate/20 for bufsize */
#define BUFSIZE 4000000

/* structures dmadb datatype  */
typedef struct{
  int nbufs; /* number of buffers to acquire */
  int fSize; /* file size of data */
  int nSecs; /* number of seconds to acquire */
  int buf;
  char daemon; /*daemon mode */
  char *ObsId;
  char *data;
}dmadb_t;