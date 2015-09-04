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

#include "timing.h"

#define LLC 3
using namespace std;

const char * queue_fifo = "/tmp/plumber";
const char * log_file = "/tmp/plumber.log";

using Allocator = CacheLineAllocator;
using Line = CacheLine;

class Busy : exception {};

typedef struct TouchInfo {
	volatile int touchSet;
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
public:
	Allocator& allocator;
	volatile Line::arr partitionsArray;

	TouchInfo info;

	pthread_mutex_t mutex;
	pthread_cond_t cv;

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

	static TouchInfo defaultInfo() {
		TouchInfo res;
		res.op = TouchInfo::OP_TOUCH;
		res.touchSet 		 = -1;		// All sets
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
			if(inputInfo.touchSet < 0) {
				lineList = allocator.getAllSets(info.touchLinesPerSet);
			} else {
				lineList = allocator.getSet(info.touchSet, info.touchLinesPerSet);
			}
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

volatile bool TouchWorker::touchForever = false;

bool cmparg(const char* arg, const char* option1, const char* option2 = NULL) {
	if (strcmp(arg, option1) == 0) {
		return true;
	}

	if(option2 != NULL && strcmp(arg, option2) == 0) {
		return true;
	}

	return false;
}

unsigned long getNumberArgument(int argc, const char* argv[], unsigned long defaultValue,
		const char* option1, const char* option2 = NULL) {
	for(int i = 1; i < argc; i++) {
		if (cmparg(argv[i], option1, option2)) {
			if(++i >= argc) break;
			istringstream ( argv[i] ) >> defaultValue;
		}
		break;
	}

	return defaultValue;
}

bool getBoolArgument(int argc, const char* argv[],
		const char* option1, const char* option2 = NULL) {
	for(int i = 1; i < argc; i++) {
		if (cmparg(argv[i], option1, option2)) {
			return true;
		}
	}

	return false;
}

int main(int argc, const char* argv[]) {
	auto linesPerSet   = getNumberArgument(argc, argv, 0, "--lines-per-set", "-l");  // According to actual ways in the CPU
	auto availableWays = getNumberArgument(argc, argv, 2, "--ways",          "-w");
	auto deamonize     = getBoolArgument  (argc, argv,    "--deamon",        "-d");
	auto verbose       = getBoolArgument  (argc, argv,    "--verbose",       "-v");
	auto doBenchmark   = getBoolArgument  (argc, argv,    "--benchmark");
	auto fake 		   = getBoolArgument  (argc, argv,    "--fake");

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
			a.allocateSet(0, a.getLinesPerSet());
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
		TouchWorker touch(a);
		touch.startTouchThread();

		while(msg.readQueue()) {
			try {
				std::cout << "[RECEIVE] " << msg.getRawMessage() << endl;

				string op = msg.popStringToken();
				if(op == "q" || op == "quit") {
					return 0;
				} else if(op == "t" || op == "touch") {
					auto t = touch.defaultInfo();

					while(msg.haveTokens()) {
						string touchOp = msg.popStringToken();
						if(touchOp == "set" || touchOp == "s") {
							t.touchSet = msg.popNumberToken();
						} else if(touchOp == "lines" || touchOp == "l") {
							t.touchLinesPerSet = msg.popNumberToken();
						} else if(touchOp == "partitions" || touchOp == "p") {
							t.partitions = msg.popNumberToken();
						} else if(touchOp == "disable-interrupts") {
							t.disableInterupts = true;
						} else if(touchOp == "stop") {
							t.op = TouchInfo::OP_STOP;
						} else if(touchOp == "flush") {
							t.op = TouchInfo::OP_FLUSH;
						} else if(touchOp == "flush-before") {
							t.flushBefore = true;
						} else if(touchOp == "flush-after") {
							t.flushAfter = true;
						} else {
							throw UnknownOperation(op + " " + touchOp);
						}
					}

					if(t.op == TouchInfo::OP_TOUCH || t.op == TouchInfo::OP_FLUSH) {
						touch.sendJob(t);
					} else if(t.op == TouchInfo::OP_STOP) {
						TouchWorker::touchForever = false;
					}
				} else {
					throw UnknownOperation(op);
				}
			} catch (OutOfTokens& e) {
				std::cout << "[MSG ERROR] Out of tokens" << endl;
			} catch (UnknownOperation& e) {
				std::cout << "[MSG ERROR] " << e.what() << ": " << e.op() << endl;
			} catch (Busy& e) {
				std::cout << "[BUSY] Already running a touch work" << endl;
			}
		}
	} catch (exception& e) {
		std::cout << endl << "[EXCEPTION] " << e.what() << endl;
		ret = 1;
	}

	if(deamonize) {
		finilize_deamon();
	}

	return ret;
}
