/*
 * TouchWorker.hpp
 *
 *  Created on: Sep 6, 2015
 *      Author: liran
 */

#ifndef TOUCHWORKER_HPP_
#define TOUCHWORKER_HPP_

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#include <fstream>

#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#include "Messages.h"
#include "lineallocator.hpp"
#include "deamon.h"

#include "timing.h"

using namespace std;

using Allocator = CacheLineAllocator;
using Line = CacheLine;

class Busy : exception {};

typedef struct TouchInfo {
	volatile int beginSet;
	volatile int endSet;
	volatile unsigned int touchLinesPerSet;
	volatile unsigned long partitions;
	volatile bool disableInterupts;
	volatile bool flushBefore;
	volatile bool flushAfter;

	volatile enum {
		OP_TOUCH, OP_FLUSH, OP_STOP
	} op;
} TouchInfo;

class TouchWorker {
private:
	Allocator& allocator;
	volatile Line::arr partitionsArray;

	TouchInfo info;

	pthread_mutex_t mutex;
	pthread_cond_t cv;

public:
	volatile static bool touchForever;

public:
	TouchWorker(Allocator& allocator) : allocator(allocator), partitionsArray(NULL) {
		mutex = PTHREAD_MUTEX_INITIALIZER;
		pthread_cond_init(&cv, NULL);
		restart();
	}

	~TouchWorker() {
		discardPartitionsArray();
	}

	TouchInfo defaultInfo() {
		TouchInfo res;
		res.op = TouchInfo::OP_TOUCH;
		res.beginSet 		 = 0;
		res.endSet	 		 = allocator.getSetsCount()-1;
		res.touchLinesPerSet = 1;
		res.partitions		 = 1;
		res.disableInterupts = false;
		res.flushBefore 	 = false;
		res.flushAfter 		 = false;
		return res;
	}

	void discardPartitionsArray() {
		if(partitionsArray != NULL) {
			delete[] partitionsArray;
			partitionsArray = NULL;
		}
	}

	void allocatePartitionsArray(vector<CacheLine::lst> partitions) {
		discardPartitionsArray();
		partitionsArray = new CacheLine::ptr[partitions.size()];
		for(unsigned int i=0; i < partitions.size(); i++) {
			partitionsArray[i] = partitions[i].front();
		}
	}

	void restart() {
		info = defaultInfo();
		discardPartitionsArray();
	}

	void sendJob(const TouchInfo& inputInfo) {
		bool locked = trylock();
		if(!locked) {
			throw Busy();
		}

		info = inputInfo;
		unsigned long length = 0;

		Line::lst lineList;

		try {
			lineList = allocator.getSets(info.beginSet, info.endSet, info.touchLinesPerSet);
			length = lineList.size();
			auto partitions = lineList.partition(info.partitions);
			allocatePartitionsArray(partitions);

			// Signal only if successful
			pthread_cond_signal(&cv);
		} catch(exception& e) {
			std::cout << "Failed allocation of set(s): " << e.what() << endl;
			restart();
		}
		unlock();

		std::cout << "[JOB] Length: " << length << endl;
	}

	void lock() {
		pthread_mutex_lock( &mutex );
	}

	bool trylock() {
		return pthread_mutex_trylock( &mutex ) == 0;
	}

	void unlock() {
		pthread_mutex_unlock( &mutex );
	}

	bool waitForJob() {
		pthread_cond_wait(&cv, &mutex);
		return true;
	}

	void startTouchThread() {
		lock();
		pthread_t thread_id;
		int res = pthread_create(&thread_id, NULL, touchWorkerThread, this);
		if (res) {
			std::cout << "[ERROR] Failed creating thread: " << res << endl;
		}
		unlock();
	}

private:
	static void* touchWorkerThread(void* p) {
		TouchWorker* t = reinterpret_cast<TouchWorker*>(p);
		t->workerThread();
		return NULL;
	}

	void flushPartitionsArray() {
		if(partitionsArray != NULL) {
			for(unsigned int i = 0; i < info.partitions; i++) {
				partitionsArray[i]->flushSets();
			}
		}
	}

	void workerThread() {
		lock();
		while(waitForJob()) {
			if(partitionsArray != NULL) {
				auto start = gettime();
				switch(info.op) {
				case TouchInfo::OP_TOUCH:
					if(info.flushBefore) { flushPartitionsArray(); }

					CacheLine::polluteSets(partitionsArray, info.partitions, TouchWorker::touchForever,
							info.disableInterupts);

					if(info.flushAfter) { flushPartitionsArray(); }
					break;

				case TouchInfo::OP_FLUSH:
					flushPartitionsArray();
					break;
				default:
					continue;
				}
				auto end = gettime();
				auto duration = timediff(start, end);

				double timeMin = (double)duration.tv_sec/60.;
				std::cout << std::fixed << std::setprecision(2) << dec;
				std::cout << endl << "Touch duration: " << timeMin << " Minutes (" << duration.tv_sec << " sec. and " << duration.tv_nsec << " nsec.)" << endl;
			}
		}
		unlock();
	}
};




#endif /* TOUCHWORKER_HPP_ */
