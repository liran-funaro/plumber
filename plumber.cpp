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
	volatile unsigned long touchIterations;
	volatile unsigned long eachSetRuns;
	volatile bool useMemFence;
	volatile bool disableInterupts;
	volatile bool flushBefore;
	volatile bool flushAfter;

	volatile enum {
		OP_TOUCH, OP_FLUSH, OP_STOP
	} op;
} TouchInfo;

class TouchWorker {
public:
	TouchInfo info;

	volatile Line::ptr startLine;

	Allocator& allocator;

	pthread_mutex_t mutex;
	pthread_cond_t cv;

	volatile static bool touchForever;

public:
	TouchWorker(Allocator& allocator) : allocator(allocator) {
		mutex = PTHREAD_MUTEX_INITIALIZER;
		pthread_cond_init(&cv, NULL);

		restart();
	}

	static TouchInfo defaultInfo() {
		TouchInfo res;
		res.op = TouchInfo::OP_TOUCH;
		res.touchIterations  = 0;		// Forever
		res.touchSet 		 = -1;		// All sets
		res.eachSetRuns		 = 1;
		res.touchLinesPerSet = 1;
		res.useMemFence 	 = true;
		res.disableInterupts = false;
		res.flushBefore 	 = false;
		res.flushAfter 		 = false;
		return res;
	}

	void restart() {
		info = defaultInfo();
		startLine = NULL;
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
			startLine = lineList.front();
			length = lineList.size();

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

	void workerThread() {
		lock();
		while(waitForJob()) {
			if(startLine != NULL) {
				auto start = gettime();
				switch(info.op) {
				case TouchInfo::OP_TOUCH:
					if(info.flushBefore) { startLine->flushSets(); }

					startLine->polluteSets(info.touchLinesPerSet, info.touchIterations, TouchWorker::touchForever,
							info.eachSetRuns, info.useMemFence, info.disableInterupts);

					if(info.flushAfter) { startLine->flushSets(); }
					break;

				case TouchInfo::OP_FLUSH:
					startLine->flushSets();
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

int main(int argc, const char* argv[]) {
	unsigned long linesPerSet = 0;  // According to actual ways in the CPU
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
						} else if(touchOp == "iterations" || touchOp == "i") {
							t.touchIterations = msg.popNumberToken();
						} else if(touchOp == "no-fence") {
							t.useMemFence = false;
						} else if(touchOp == "disable-interrupts") {
							t.disableInterupts = true;
						} else if(touchOp == "set-i") {
							t.eachSetRuns = msg.popNumberToken();
						} else if(touchOp == "forever" || touchOp == "f") {
							t.touchIterations = 0;
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
		unlink(queue_fifo);
		finilize_deamon();
	}

	return ret;
}
