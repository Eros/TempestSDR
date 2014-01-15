#ifndef _Circubuffer
#define _Circubuffer

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "threading.h"

typedef struct CircBuff CircBuff_t;

#define CB_OK (1)
#define CB_FULL (0)
#define CB_EMPTY (0)

struct CircBuff
{
	float * buffer; // the circular buffer itself
	int buffer_size; // the size of the circular buffer
	int desired_buf_size; // the size of the buffer that we want it to become

    volatile int remaining_capacity; // the available capacity. I.e. how many free elements are there in the buffer
    int pos; // the position where the next element will be inserted
    int rempos; // the position where the next element will be taken from

    volatile int is_waiting;

    mutex_t mutex; // for thread safety
    mutex_t locker; // for waiting
};

void cb_init(CircBuff_t * cb);
int cb_add(CircBuff_t * cb, float * buff, const int size);
int cb_rem_blocking(CircBuff_t * cb, float * in, const int len);
void cb_free(CircBuff_t * cb);

#endif