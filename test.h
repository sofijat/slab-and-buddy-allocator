#pragma once

#define MASK (0xA5)
#include "slab.h"

struct data_s {
	int id;
	kmem_cache_t *shared;
	int iterations;
};

