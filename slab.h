#ifndef _SLAB_H_
#define _SLAB_H_
#include "buddy.h"
#include <stdio.h>

#define BLOCK_SIZE (4096)
#define CACHE_L1_LINE_SIZE (64)
static buddy_t *buddy;
static void* this_space;
static void* actual_space;

typedef struct slab_t;

typedef struct kmem_cache_s{

	slab_t* full;
	slab_t* partial;
	slab_t* free;

	unsigned int objSize; //velicina svakog objekta koji se stavlja u slab
	unsigned int num_obj_slab; //broj objekata u svakom slabu
	unsigned int total_num_obj;  //ukupan broj objekata, kad se doda slab, ovo se poveca za broj objekata u slabu
	unsigned int num_blocks_in_slab; //broj blokova u jednom slabu, onaj lik je cuvao stepen, ja cuvam ovo


	size_t color; //za poravnjanje, dobija se deljenjem slobodnih bajtova sa 64
	unsigned int color_off;   // = CACHE_L1_LINE_SIZE;
	unsigned int color_next; //vraca se na 0 kad dostigne color

	void(*ctor)(void*);
	void(*dtor)(void*);

	const char* name;
	struct kmem_cache_s *next;

	//bool shrink;
	unsigned int error_code;

	std::mutex* mutex_p;
	std::mutex mutex;
} kmem_cache_t;


struct slab_t{
	slab_t *next;
	int objSize; //velicina objekta
	void* s_mem; //pokazivac na prvi objekat
	kmem_cache_t* my_cache; //kes kome pripada
	unsigned int first_free; //indeks prvog slobodnog objekta u slabu
	unsigned int num_active; //br aktivnih objekata u slabu
};

struct small_buffer{
	size_t obj_size;
	kmem_cache_t* cachep;
};

typedef struct small_buffer small_buffer_t;

void kmem_init(void *space, int block_num);
kmem_cache_t *kmem_cache_create(const char *name, size_t size, void(*ctor)(void*), void(*dtor)(void*));
int kmem_cache_shrink(kmem_cache_t *cachep);
void *kmem_cache_alloc(kmem_cache_t *cachep);
void kmem_cache_free(kmem_cache_t *cachep, void *objp);
void *kmalloc(size_t size);
void kfree(const void* objp);
void kmem_cache_destroy(kmem_cache_t *cachep);
void kmem_cache_info(kmem_cache_t *cachep);
int kmem_cache_error(kmem_cache_t *cachep);

#endif 

