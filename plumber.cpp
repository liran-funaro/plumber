/*
 * cleancache.cpp
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

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

#define LLC 3
using namespace std;

const char * queue_fifo = "/tmp/plumber";
const char * log_file = "/tmp/plumber.log";

using Allocator = CacheLineAllocator<64>;
using Line = CacheLine<64>;

class TouchInfo {
public:
	int touchSet;
	unsigned int touchLinesPerSet;
	int touchIterations;
	Allocator& allocator;

	pthread_mutex_t mutex;
	pthread_cond_t cv;

	volatile static bool touchForever;

public:
	TouchInfo(Allocator& allocator) : allocator(allocator) {
		mutex = PTHREAD_MUTEX_INITIALIZER;
		pthread_cond_init(&cv, NULL);

		touchSet = -1;
		touchLinesPerSet = allocator.getWaysCount();
		touchIterations = 1;
	}

	unsigned long getTouchArraySize() {
		unsigned long touchArraySize = touchLinesPerSet;
		if(touchSet < 0) {
			touchArraySize *= allocator.getSetsCount();
		}

		return touchArraySize;
	}

	Line::ptr allocate(Line::arr lines = NULL) {
		Line::ptr res = NULL;
		lock();
		try {
			if(touchSet < 0) {
				res = allocator.getAllSets(lines, touchLinesPerSet);
			} else {
				res = allocator.getSet(touchSet, lines, touchLinesPerSet);
			}
		} catch(exception& e) {
			std::cout << "Failed allocation of set(s): " << e.what() << endl;
			res = NULL;
		}
		pthread_cond_signal(&cv);
		unlock();

		return res;
	}

	void lock() {
		pthread_mutex_lock( &mutex );
	}

	void unlock() {
		pthread_mutex_unlock( &mutex );
	}

	void waitForAllocation() {
		pthread_cond_wait(&cv, &mutex);
	}

	void startTouchThread() {
		lock();
		pthread_t thread_id;
		int res = pthread_create(&thread_id, NULL, touchWorkerThread, this);
		if (res) {
			std::cout << "[ERROR] Failed creating thread: " << res << endl;
		} else {
			waitForAllocation();
		}
		unlock();
	}

	static void* touchWorkerThread(void* p) {
		TouchInfo* t = reinterpret_cast<TouchInfo*>(p);

		unsigned int touchLinesPerSet = t->touchLinesPerSet;
		int touchIterations = t->touchIterations;

		Line::ptr line = t->allocate();

		if(line != NULL) {
			auto start = gettime();
			if(touchIterations > 0) {
				line->polluteSets(touchLinesPerSet, touchIterations);
			} else {
				line->polluteSets(touchLinesPerSet, TouchInfo::touchForever);
			}
			auto end = gettime();
			auto duration = timediff(start, end);

			double timeMin = (double)duration.tv_sec/60.;
			std::cout << std::fixed << std::setprecision(2) << dec;
			std::cout << endl << "Touch duration: " << timeMin << " Minutes (" << duration.tv_sec << " sec. and " << duration.tv_nsec << " nsec.)" << endl;
		}

		return NULL;
	}
};

volatile bool TouchInfo::touchForever = false;

bool cmparg(const char* arg, const char* option1, const char* option2 = NULL) {
	if (strcmp(arg, option1) == 0) {
		return true;
	}

	if(option2 != NULL && strcmp(arg, option2) == 0) {
		return true;
	}

	return false;
}

int main(int argc, const char* argv[]) {
	unsigned long linesPerSet = 16;
	unsigned int availableWays = 2;
	bool doBenchmark = false;
	bool fake = false;
	bool deamonize = false;
	bool verbose = false;

	for(int i = 1; i < argc; i++) {
		if (cmparg(argv[i], "--lines-per-set", "-l")) {
			i++;
			if(i >= argc) break;
			istringstream ( argv[i] ) >> linesPerSet;
		} else if (cmparg(argv[i], "--ways", "-w")) {
			i++;
			if(i >= argc) break;
			istringstream ( argv[i] ) >> availableWays;
		} else if(cmparg(argv[i], "--benchmark")) {
			doBenchmark = true;
		} else if(cmparg(argv[i], "--fake", "-f")) {
			fake = true;
		} else if(cmparg(argv[i], "--deamon", "-d")) {
			deamonize = true;
		} else if(cmparg(argv[i], "--verbose", "-v")) {
			verbose = true;
		}
	}

	if(deamonize) {
		daemonize("plumber", NULL, log_file);
	}

	int ret = 0;
	try {
		////////////////////////////////////////////////////////////////////////
		// Allocation
		////////////////////////////////////////////////////////////////////////
		auto start = gettime();
		Allocator a = Allocator(LLC, linesPerSet, availableWays, verbose);
		if(!fake) {
			a.allocateAllSets();
		} else{
			a.allocateSet(0, linesPerSet);
		}
		auto end = gettime();

		auto duration = timediff(start, end);

		double timeMin = (double)duration.tv_sec/60.;
		std::cout << std::fixed << std::setprecision(2) << dec;
		std::cout << endl << "Allocation duration: " << timeMin << " Minutes (" << duration.tv_sec << " sec. and " << duration.tv_nsec << " nsec.)" << endl;

		if(doBenchmark) {
			return 0;
		}

		////////////////////////////////////////////////////////////////////////
		// Message Loop
		////////////////////////////////////////////////////////////////////////
		Messages msg(queue_fifo);

		while(msg.readQueue()) {
			try {
				std::cout << "[RECEIVE] " << msg.getRawMessage() << endl;

				string op = msg.popStringToken();
				if(op == "q" || op == "quit") {
					return 0;
				} else if(op == "t" || op == "touch") {
					TouchInfo t(a);

					bool stopOperation = false;
					while(msg.haveTokens()) {
						string touchOp = msg.popStringToken();
						if(touchOp == "set" || touchOp == "s") {
							t.touchSet = msg.popNumberToken();
						} else if(touchOp == "lines" || touchOp == "l") {
							t.touchLinesPerSet = msg.popNumberToken();
						} else if(touchOp == "iterations" || touchOp == "i") {
							t.touchIterations = msg.popNumberToken();
						} else if(touchOp == "forever" || touchOp == "f") {
							t.touchIterations = -1;
							TouchInfo::touchForever = true;
						} else if(touchOp == "stop") {
							TouchInfo::touchForever = false;
							stopOperation = true;
						} else {
							throw UnknownOperation(op + " " + touchOp);
						}
					}

					if(!stopOperation) {
						t.startTouchThread();
					}
				} else {
					throw UnknownOperation(op);
				}
			} catch (OutOfTokens& e) {
				std::cout << "[MSG ERROR] Out of tokens" << endl;
			} catch (UnknownOperation& e) {
				std::cout << "[MSG ERROR] " << e.what() << ": " << e.op() << endl;
			}
		}
	} catch (exception& e) {
		std::cout << endl << "[EXCEPTION] " << e.what() << endl;
		ret = 1;
	}

	if(deamonize) {
		unlink(queue_fifo);
		finilize_deamon();
	}

	return ret;
}
