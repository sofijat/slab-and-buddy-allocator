#include "buddy.h"
#include <math.h>
#include <mutex>



buddy_t* init_buddy(void *space, int max_order) {
	
	buddy_t* bt = (buddy_t*)space;
	bt->max_order = max_order;
	bt->num_blocks = pow((double)2, max_order);
	bt->avail = (block_t*)((buddy_t*)space + 1);
	bt->used_bits = (unsigned long *)(bt->avail + (max_order+1));
	bt->mutex_p = new((void*)(&bt->mutex)) std::mutex();

	for (int i = 0; i < max_order+1; i++){  //
		bt->avail[i].next = NULL;
	}

	//racunanje koliko treba unsigned longova
	int num_of_longs = 0;
	num_of_longs = (int)(bt->num_blocks / 32);

	//nijedan blok nije zauzet
	for (int i = 0; i < num_of_longs; i++){
		bt->used_bits[i] =0;  
	}

	//nema slobodnih listi osim za maxorder
	for (int i = 0; i < max_order + 1; i++){
		bt->avail[i].next = NULL;
	}

	bt->mem_start = (unsigned long)(bt->used_bits + num_of_longs);
	bt->mem_start += BLOCK_SIZE-1;  //zameniti sa BLOCK_SIZE?
	bt->mem_start &= ~(BLOCK_SIZE-1); //zameniti sa BLOCK_SIZE?

	block_t * fgp = (block_t*)bt->mem_start; //jedan najveci blok slobodan, ulancan
	fgp->order = max_order;
	fgp->next = NULL;
	bt->avail[max_order].next = fgp;
	
	printf_s("Buddy memory pool (2^%d) created!\n", fgp->order);
	return bt;
}

void update_bits_to_zero(buddy_t *buddy, block_t *block){  //postaviti bit na 0, tj slobodan je
	unsigned int id = 0;
	id = ((unsigned long)block - buddy->mem_start) /4096;
	if (id >= buddy->num_blocks) return;

	int i = (int)(id / 32);
	int j = id % 32;
	unsigned long k = 1UL << j;
	buddy->used_bits[i] &= ~k;
}

void update_bits_to_one(buddy_t *buddy, block_t *block){  //postaviti bit na 1, tj zauzet je
	unsigned int id = 0;
	id = (int)(((unsigned long)block - buddy->mem_start) / BLOCK_SIZE);
	if (id >= buddy->num_blocks) return;

	int i = (int)(id / 32);
	int j = id % 32;
	unsigned long k = 1UL << j;
	buddy->used_bits[i] |= k;
}

bool test(buddy_t* buddy, block_t* block){ //vraca TRUE ako je zauzet
	unsigned int id = 0;
	id = (int)(((unsigned long)block - buddy->mem_start) / BLOCK_SIZE);
	if (id >= buddy->num_blocks) return false;

	int i = (int)(id / 32);
	int j = id % 32;
	unsigned long k = 1UL << j;
	bool ret = buddy->used_bits[i] & k;
	return ret;
}

block_t* buddy_alloc(buddy_t *buddy, unsigned int order){

	if (buddy == NULL) return NULL;
	buddy->mutex.lock();
	if (order > buddy->max_order){
		buddy->mutex.unlock();
		return NULL;
	}

	block_t *block=NULL;
	unsigned int ord=order;

	while (ord <= buddy->max_order){ 
		if (buddy->avail[ord].next==NULL){ 
			ord++;
		}
		else {
			break;
		}
	}

	if (ord > buddy->max_order) {
		printf_s("That amount space is currently unavailable, delete some blocks first!\n");
		buddy->mutex.unlock();
		return NULL; //nista nije slobodno
	}

	block = buddy->avail[ord].next;
	if (buddy->avail[ord].next && (buddy->avail[ord].next)->next) buddy->avail[ord].next = (buddy->avail[ord].next)->next;
	else buddy->avail[ord].next = NULL;
	update_bits_to_one(buddy, block); //zauzimanje=1

	//ako je zauzeo veci nego sto mu je potrebno
	while (ord > order){
		ord--;
		
		unsigned long num = pow((double)2, (int)ord);
		block_t* blk;
		blk = (block_t*)((unsigned long)block+num*4096);
		blk->order = ord;
		blk->next = buddy->avail[ord].next; //NULL
		buddy->avail[ord].next = blk;
		update_bits_to_zero(buddy, blk);  //0=slobodno		
	}	
	
	block->order = ord;
	buddy->mutex.unlock();
	return block;
}

block_t* find_buddy(buddy_t *buddy, block_t *block){
	unsigned long new_block;
	unsigned long new_buddy;

	if ((unsigned long)block < buddy->mem_start) return NULL;
	
	new_block = (unsigned long)block - buddy->mem_start;
	unsigned long buddy_num = pow ((double)2, block->order);

	new_buddy = new_block ^ (buddy_num * BLOCK_SIZE);
	return (block_t *)(new_buddy + buddy->mem_start);
}

void buddy_free(buddy_t *buddy, block_t* block){
	
	int order;
	if (buddy == NULL) return;

	buddy->mutex.lock();
	if (block->order > buddy->max_order) {
		buddy->mutex.unlock();
		return;
	}
	if (block->order < 0) order = 0;
	else order = block->order;

	update_bits_to_zero(buddy, block);	
	block_t* my_buddy = find_buddy(buddy, block);
	
	if (my_buddy != NULL){
		while ((test(buddy, my_buddy) == false) && order < buddy->max_order){ //ako je buddy neiskoriscen
			if (buddy->avail[order].next && (buddy->avail[order].next)->next) buddy->avail[order].next = (buddy->avail[order].next)->next;
			else buddy->avail[order].next = NULL;
			order++;
			block->order = order;
			if (order >= buddy->max_order) break;
			my_buddy = find_buddy(buddy, block);
			if (my_buddy == NULL) break;
		}
	}
	block->next = buddy->avail[order].next;
	buddy->avail[order].next = block;
	buddy->mutex.unlock();
	//printf_s("Block sucessfully deleted\n");
}