#include "ipcutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#define MAX_RETRIES 5

union semun {
    int val;
    struct semid_ds *buf;
    ushort *array;
};

// #define _DEBUG 1

/* *************************************************************** */
/*!
  Returns a shared memory block and its shmid
*/
void* ipc_alloc (key_t key, size_t size, int flag, int* shmid)
{
  void* buf = 0;
  int id = 0;

  id = shmget (key, size, flag);
  if (id < 0) {
    fprintf (stderr, "ipc_alloc: shmget (key=%x, size=%d, flag=%x) %s\n",
             key, size, flag, strerror(errno));
    return 0;
  }

#ifdef _DEBUG
  fprintf (stderr, "ipc_alloc: shmid=%d\n", id);
#endif

  buf = shmat (id, 0, flag);

  if (buf == (void*)-1) {
    fprintf (stderr,
	     "ipc_alloc: shmat (shmid=%d) %s\n"
	     "ipc_alloc: after shmget (key=%x, size=%d, flag=%x)\n",
	     id, strerror(errno), key, size, flag);
    return 0;
  }

#ifdef _DEBUG
  fprintf (stderr, "ipc_alloc: shmat=%p\n", buf);
#endif

  if (shmid)
    *shmid = id;

  return buf;
}

void ipc_delete(void * semptr, int id){
  shmdt(semptr);
  shmctl(id, IPC_RMID, NULL);
}

void sem_delete(int id){
  semctl(id, 0, IPC_RMID, NULL) ;
}

int ipc_semop (int semid, short num, short op, short flag)
{
  struct sembuf semopbuf;

  semopbuf.sem_num = num;
  semopbuf.sem_op = op;
  semopbuf.sem_flg = flag;
 
  if (semop (semid, &semopbuf, 1) < 0) {
    if (!(flag | IPC_NOWAIT))
      perror ("ipc_semop: semop");
    return -1;
  }
  return 0;
}

int initsem(key_t key, int nsems)  /* key from ftok() */
{
    int i;
    union semun arg;
    struct semid_ds buf;
    struct sembuf sb;
    int semid;

    semid = semget(key, nsems, IPC_CREAT | IPC_EXCL | 0666);

    if (semid >= 0) { /* we got it first */
        sb.sem_op = 1; sb.sem_flg = 0;
        arg.val = 1;

        for(sb.sem_num = 0; sb.sem_num < nsems; sb.sem_num++) { 
            /* do a semop() to "free" the semaphores. */
            /* this sets the sem_otime field, as needed below. */
            if (semop(semid, &sb, 1) == -1) {
                int e = errno;
                semctl(semid, 0, IPC_RMID); /* clean up */
                errno = e;
                return -1; /* error, check errno */
            }
        }

    } else if (errno == EEXIST) { /* someone else got it first */
        int ready = 0;

        semid = semget(key, nsems, 0); /* get the id */
        if (semid < 0) return semid; /* error, check errno */

        /* wait for other process to initialize the semaphore: */
        arg.buf = &buf;
        for(i = 0; i < MAX_RETRIES && !ready; i++) {
            semctl(semid, nsems-1, IPC_STAT, arg);
            if (arg.buf->sem_otime != 0) {
                ready = 1;
            } else {
                sleep(1);
            }
        }
        if (!ready) {
            errno = ETIME;
            return -1;
        }
    } else {
        return semid; /* error, check errno */
    }

    return semid;
}
