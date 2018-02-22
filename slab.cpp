#include "slab.h"
#include <math.h>
#include <stdlib.h>
#include <cstring>
#include <mutex>

typedef unsigned int kmem_bufctl_t;
#define slab_bufctl(slabp)((kmem_bufctl_t*)(((slab_t*)slabp) + 1))
std::mutex list_mutex;

////////// FUNCTIONS ////////////
void init_slab_bufctl(slab_t* slabp){
	int i;
	for (i = 0; i < slabp->my_cache->num_obj_slab; i++){
		slab_bufctl(slabp)[i] = i + 1;
	}
	slab_bufctl(slabp)[i - 1] = -1;
}
bool check_full(slab_t* slab){
	bool ret = false;
	if (slab->first_free == -1) ret = true;
	return ret;
}
void initialize_fields(kmem_cache_t* cache) {
	cache->full = NULL;
	cache->partial = NULL;
	cache->free = NULL;

	cache->num_obj_slab = 0;
	cache->total_num_obj = 0;
	cache->num_blocks_in_slab = 0;

	cache->color = 0; //broj slobodnih bajtova/64
	cache->color_off = CACHE_L1_LINE_SIZE;
	cache->color_next = 0;  //resetujese na 0 kad dostigne color 

	//cache->shrink = false;
	cache->error_code = 0;
	cache->mutex_p = new((void*)(&cache->mutex)) std::mutex();
}
void delete_from_list(slab_t* slabp, int list) { //1=partial, 2=full, 3=free
	if (slabp == NULL) return;
	slab_t* help=NULL;
	if (list == 1 && slabp->my_cache->partial != NULL) {
		if (slabp == slabp->my_cache->partial) {
			slabp->my_cache->partial = slabp->my_cache->partial->next;
			//slabp = NULL;
			return;
		}
		if (slabp->my_cache->partial != NULL) help = slabp->my_cache->partial;
		while (help->next != slabp && help != NULL) help = help->next;
	}
	else if (list == 2) {
		if (slabp == slabp->my_cache->full) {
			slabp->my_cache->full = slabp->my_cache->full->next;
			//slabp = NULL;
			return;
		}
		if (slabp->my_cache->full != NULL) help = slabp->my_cache->full;
		while (help->next != slabp && help != NULL) help = help->next;
	}
	else if (list == 3) {
		if (slabp == slabp->my_cache->free) {
			slabp->my_cache->free = slabp->my_cache->free->next;
			//slabp = NULL;
			return;
		}
		if (slabp->my_cache->free != NULL) help = slabp->my_cache->free;
		while (help->next != slabp && help != NULL) help = help->next;
	}

	//help je jedan elem ispred onog koji se uklanja
	if (help != NULL) {
		help->next = help->next->next;
		//slabp = NULL;
	}
}
/////////////////////////////////

int kmem_cache_estimate(kmem_cache_t* cacep){
	unsigned int available = BLOCK_SIZE - (sizeof(block_t)+sizeof(slab_t)+sizeof(unsigned int)); //za 1 blok
	int num = 0;  //broj objekata u slabu
	unsigned int wasted = 0;

	if (cacep->objSize < available){
		cacep->num_blocks_in_slab = 1;
		num = (int)(available / (cacep->objSize + sizeof(unsigned int)));
	}
	else {
		int order = 0;
		while (cacep->objSize>available){
			order++;
			available *= 2;
		}
		if (order > buddy->max_order){
			cacep->error_code = 1; //ERROR
			return 0;
		}
		
		cacep->num_blocks_in_slab = pow((double)2, order);
		num = 1;
	}

	wasted = available - num*(cacep->objSize + sizeof(unsigned int));
	cacep->num_obj_slab = num;
	if (wasted>cacep->color_off) cacep->color = (size_t)(wasted / cacep->color_off);
	else cacep->color = 0;
	cacep->color_next = 0;
}

void kmem_init(void *space, int block_num){

	this_space = space;
	int numOfBlocks = block_num;
	kmem_cache_t* cache_cache = (kmem_cache_t*)space;
	initialize_fields(cache_cache);

	cache_cache->objSize = sizeof(kmem_cache_t);

	cache_cache->ctor = NULL;
	cache_cache->dtor = NULL;

	cache_cache->name = "cache_cache";
	cache_cache->next = NULL;

	//Za male memorijske bafere kojih ima 13
	small_buffer_t* buf = (small_buffer_t*)((kmem_cache_t*)space + 1);
	for (int i = 0; i < 13; i++){
		buf[i].obj_size = pow((double)2, i + 5);
		buf[i].cachep = NULL;  //pripada kesu koji ima datu velicinu objekta
	}
	buf += 13;

	//inicijalizacija buddy-ja
	unsigned int max_order = 0;
	max_order = (unsigned int)(log2((double)block_num));
	buddy = init_buddy((void*)buf, max_order);

	actual_space = (void*)((char*)space + BLOCK_SIZE);
	kmem_cache_estimate(cache_cache);
}

slab_t* create_new_slab(kmem_cache_t* cachep){
	slab_t* sh = NULL;
	block_t* block= NULL;
	if (cachep == NULL) return NULL;

	block = buddy_alloc(buddy, log2((double)cachep->num_blocks_in_slab));
	if (!block) {
		cachep->error_code = 2;  //ERROR
		return NULL;
	}
	sh = (slab_t*)(block + 1);
	sh->my_cache = cachep;
	
	if (!cachep->free) {
		cachep->free = sh;
		sh->next = NULL;
	}
	else {
		sh->next = cachep->free->next;
		cachep->free = sh;
	}
	sh->objSize = cachep->objSize;

	unsigned int offset = cachep->color_next*cachep->color_off;
	cachep->color_next++;
	if (cachep->color_next > cachep->color) cachep->color_next = 0;

	sh->s_mem = (void *)((unsigned char*)(((unsigned int*)(sh + 1)) + cachep->num_obj_slab) + offset);
	sh->num_active = 0;

	sh->my_cache->total_num_obj += sh->my_cache->num_obj_slab;

	//inicijalizacija objekata
	void* objp = sh->s_mem;
	for (int i = 1; i < cachep->num_obj_slab; i++){
		if (cachep->ctor != NULL) cachep->ctor(objp);
		objp = (void*)((char*)objp + sh->objSize);
	}

	init_slab_bufctl(sh);
	sh->first_free = 0;
	return sh;

}

kmem_cache_t *kmem_cache_create(const char *name, size_t size, void(*ctor)(void*), void(*dtor)(void*)){

	if (name == NULL) return NULL;
	void* new_space = this_space;
	kmem_cache_t* cache_cache = (kmem_cache_t*)new_space;

	kmem_cache_t* head = cache_cache;
	
	list_mutex.lock();
	while (head != NULL){
		if (head->name == name) return head;
		else head = head->next;
	}
	list_mutex.unlock();

	kmem_cache_t* k_cache = (kmem_cache_t*)kmem_cache_alloc(cache_cache); //ERROR nije uspela alokacija
	if (k_cache == NULL) {
		return NULL;
	}

	initialize_fields(k_cache);
	k_cache->objSize = size;

	k_cache->ctor = ctor;
	k_cache->dtor = dtor;

	k_cache->name = name;
	k_cache->mutex_p = new((void*)(&k_cache->mutex)) std::mutex();

	list_mutex.lock();
	k_cache->next = cache_cache->next;  //svi kesevi se ulancavaju posle cache_cache
	cache_cache->next = k_cache;
	list_mutex.unlock();

	kmem_cache_estimate(k_cache);
}

void *kmem_cache_alloc(kmem_cache_t *cachep){
	slab_t* slab = NULL;
	cachep->mutex.lock();
	if (cachep->partial == 0 && cachep->free == 0) { //napravi novi
		slab = create_new_slab(cachep); //ovo pravljenje ubacuje slab u  free listu
		//cachep->shrink = false;
	}

	if (cachep->partial != NULL){
		slab = cachep->partial;
	}
	else if (cachep->free != NULL){
		slab = cachep->free;
		cachep->free = cachep->free->next;
		slab->next = cachep->partial;
		cachep->partial = slab;
	}

	unsigned int id = slab->first_free;
	slab->num_active++;
	slab->first_free = slab_bufctl(slab)[slab->first_free];  //nalazi novi first_free
	if (check_full(slab)) { //ako je postao pun
		delete_from_list(slab, 1);
		if (cachep->full) {
			slab->next = cachep->full;
			cachep->full = slab;
		}
		else {
			cachep->full = slab;
			slab->next = NULL;
		}
	}
	cachep->mutex.unlock();
	return (void*)((char*)slab->s_mem + (slab->my_cache->objSize)*id);

}

void update_slab_bufctl(slab_t* slabp, void* objp) {
	unsigned int obj_ind = ((char*)objp - (char*)slabp->s_mem) / (slabp->my_cache->objSize);
	slab_bufctl(slabp)[obj_ind] = slabp->first_free;
	slabp->first_free = obj_ind;
}

void kmem_cache_free(kmem_cache_t *cachep, void* objp) {
	if (cachep == NULL) return;
	cachep->mutex.lock();
	if (objp == NULL) {
		cachep->error_code = 3; //ERROR
		cachep->mutex.unlock();
		return;
	}

	if (cachep->dtor != NULL) {
		cachep->dtor(objp);
	}

	//unsigned long long ad = ((unsigned long long)objp - (unsigned long long)this_space)/BLOCK_SIZE;
	unsigned long ad = ((unsigned long)objp) & (~(BLOCK_SIZE - 1));
	slab_t *slab = (slab_t*)((block_t*)ad + 1);

	update_slab_bufctl(slab, objp);
	slab->num_active--;
	if (slab->num_active == 0) {
		delete_from_list(slab, 1);  //brisemo iz parcijalne, dodajemo u slobodnu
		slab->next = slab->my_cache->free;
		slab->my_cache->free = slab;
		cachep->mutex.unlock();
		return;
	}
	if (slab->num_active == slab->my_cache->num_obj_slab - 1) {
		delete_from_list(slab, 2); //brisemo iz full, dodajemo u parcijalnu
		slab->next = slab->my_cache->partial;
		slab->my_cache->partial = slab;
		cachep->mutex.unlock();
		return;
	}
	cachep->mutex.unlock();
}

void *kmalloc(size_t size) {
	if (size > pow((double)2, 17)) { //mozda ERROR zatrazeno vise
		return NULL;
	}
	small_buffer_t *sm_buf = (small_buffer_t*)((kmem_cache_t*)this_space + 1);
	bool found = false;

	while (!found) {
		if (size > sm_buf->obj_size) {
			sm_buf++; found = false;
			continue;
		}
		else {
			if (!sm_buf->cachep) {
				char name[20] = "size-";
				char name_n[7];
				itoa(size, name_n, 10);
				sm_buf->cachep = kmem_cache_create(strcat(name, name_n), sm_buf->obj_size, NULL, NULL);
			}
			//sm_buf->cachep->cnt++; broj aktivnih u njegovom slabu da se poveca
			found = true;
		}

	}
	return kmem_cache_alloc(sm_buf->cachep);
}

void kfree(const void* objp) {  //treba zakljucavanje
	if (objp == NULL) return;
	unsigned long ad = ((unsigned long)objp) & (~(BLOCK_SIZE - 1));
	slab_t *slab = (slab_t*)((block_t*)ad + 1);
	kmem_cache_t* cache = slab->my_cache; 

	//cache->cnt--;  treba da smanji br aktiv obj.
	kmem_cache_free(cache, (void*)objp);

}

void delete_slab_list(kmem_cache_t *cachep, slab_t *slabp) {
	slab_t* curr = slabp;
	for (; curr != NULL; curr = curr->next) {
		if (cachep->dtor)
			for (int i = 0; i < cachep->num_obj_slab; i++) {
				cachep->dtor((void*)((unsigned long)curr->s_mem + i*curr->objSize));
			}
	}
	buddy_free(buddy, (block_t*)slabp - 1);
}

void kmem_cache_destroy(kmem_cache_t* cachep) {
	if (cachep == NULL) return;
	cachep->mutex.lock();
	if (cachep->free) {
		delete_slab_list(cachep, cachep->free);
		cachep->free = NULL;
	}
	if (cachep->partial) {
		delete_slab_list(cachep, cachep->partial);
		cachep->partial = NULL;
	}
	if (cachep->full) {
		delete_slab_list(cachep, cachep->full);
		cachep->full = NULL;
	}
	cachep->mutex.unlock();
	kmem_cache_t* cache_cache = (kmem_cache_t*)this_space;
	cachep->mutex_p->std::mutex::~mutex();
	kmem_cache_free(cache_cache, cachep);

	//brisanje iz liste svih keseva
	kmem_cache_t* help = cache_cache;
	list_mutex.lock();
	if (help->name != cachep->name) {
		while (help != NULL & help->next->name != cachep->name) help = help->next;
		if (help != NULL) help->next = cachep->next;
		else {
			cachep->error_code = 4;
		}
		list_mutex.unlock();
		return;
	}
	else {
		cache_cache = NULL; //treba da se obrise cache_cache
	}
	list_mutex.unlock();
	cachep->next = NULL;
}

int kmem_cache_shrink(kmem_cache_t *cachep) {
	int ret = 0;
	int i = 0;
	cachep->mutex.lock();
	if (cachep->free != NULL) {	
		for (slab_t* cur = cachep->free; cur != NULL; cur = cur->next) i++;
		delete_slab_list(cachep, cachep->free);
		cachep->free = NULL;
	}
	/*else {
		cachep->shrink = true;
	}*/
	ret = i*cachep->num_blocks_in_slab;
	cachep->mutex.unlock();
	return ret;
}

void kmem_cache_info(kmem_cache_t *cachep) {
	if (cachep == NULL) return;
	unsigned int slab_num = 0;
	unsigned int num_active_objs = 0;
	double percent = 0;
	cachep->mutex.lock();
	slab_t* slab = NULL;
	if (cachep->full != NULL) slab = cachep->full;
	while (slab != NULL) {
		slab_num++;
		num_active_objs += slab->num_active;
		slab = slab->next;
	}
	if (cachep->partial != NULL) slab = cachep->partial;
	while (slab != NULL) {
		slab_num++;
		num_active_objs += slab->num_active;
		slab = slab->next;
	}
	if (cachep->free != NULL) slab = cachep->free;
	while (slab != NULL) {
		slab_num++;
		slab = slab->next;
	}
	cachep->mutex.unlock();
	percent = ((double)num_active_objs / (double)cachep->total_num_obj) * 100.0;
	printf("Name: %s | Data size: %dB | Cache size:%d blocks | Slab number:%d | Objects in slab:%d | Percentage:%.2f%%\n", cachep->name,
		cachep->objSize, cachep->num_blocks_in_slab*slab_num, slab_num, cachep->num_obj_slab, percent);
}

int kmem_cache_error(kmem_cache_t* cachep) {
	if (cachep == NULL) {
		printf("ERROR: Cache does not exist!\n");
		return 666;
	}
	if (cachep->error_code == 1) printf("ERROR: Estimated order is bigger than buddy's max_order!\n");
	else if (cachep->error_code == 2) printf("ERROR: Buddy cannot allocate more blocks!\n ");
	else if (cachep->error_code == 3) printf("ERROR: Object does not exist!\n ");
	else if (cachep->error_code == 4) printf("ERROR: Cache does not exist!\n ");
	return cachep->error_code;
}
