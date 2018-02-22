#ifndef _BUDDY_H_
#define _BUDDY_H_
#include <mutex>
#include <stdio.h>
#include <math.h>
#define BLOCK_SIZE (4096)

typedef struct free_s{
	struct free_s* next;
	int order;
} block_t;

typedef struct buddy_s{
	unsigned long mem_start;
	int max_order;
	int num_blocks;
	unsigned long* used_bits; //1=zauzet blok, 0=slobodan blok
	block_t* avail;

	std::mutex* mutex_p;
	std::mutex mutex;
} buddy_t;

buddy_t* init_buddy(void *space, int max_order);
block_t* buddy_alloc(buddy_t *buddy, unsigned int order);  //alocira 2^order blokova
void buddy_free(buddy_t *buddy, block_t* block); //oslobadja datu grupu blokova

#endif