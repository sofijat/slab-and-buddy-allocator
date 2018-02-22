#include <vector>
#include <thread>
#include <cstdio>

#include "slab.h"
#include "test.h"

	void run_threads(int (*work)(struct data_s), void *data, int num) {
		std::vector<std::thread> threads;
		for (int i = 0; i < num; i++) {
			struct data_s private_data = *(struct data_s*) data;
			private_data.id = i + 1;
			threads.emplace_back(work, private_data);
		}

		for (int i = 0; i < num; i++) {
			threads[i].join();
		}
	}
