/*
 * benchmarking.cpp
 *
 *  Created on: Aug 27, 2015
 *      Author: liran
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

#include "pageallocator.h"
#include "timing.h"

#include "benchmarking.h"

#define LLC 3
using namespace std;

int g_dummy;

inline void mfence() {
  asm volatile("mfence");
}

// Measure the time taken to access the given address, in nanoseconds.
int time_access(void* ptr) {
	struct timespec ts0;
	clock_gettime(CLOCK_MONOTONIC, &ts0);

	g_dummy += *(volatile int *) ptr;
	mfence();

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int)((ts.tv_sec - ts0.tv_sec) * 1000000000 + (ts.tv_nsec - ts0.tv_nsec));
}

int time_lines(CacheLine<64>** lines, unsigned long numbersOfWays, unsigned long runs) {
	int* times = new int[runs];

	for (unsigned long run = 0; run < runs; run++) {
		// Ensure the first address is cached by accessing it.
		g_dummy += *(volatile int *) lines[0];
		mfence();
		// Now pull the other addresses through the cache too.
		for (unsigned int i = 1; i < numbersOfWays; i++) {
			g_dummy += *(volatile int *) lines[i];
		}
		mfence();
		// See whether the first address got evicted from the cache by
		// timing accessing it.
		times[run] = time_access(lines[0]);
	}
	// Find the median time.  We use the median in order to discard
	// outliers.  We want to discard outlying slow results which are
	// likely to be the result of other activity on the machine.
	//
	// We also want to discard outliers where memory was accessed
	// unusually quickly.  These could be the result of the CPU's
	// eviction policy not using an exact LRU policy.
	std::sort(times, &times[runs]);
	int median_time = times[runs / 2];

	delete[] times;

	return median_time;
}

void allocateLines(long set, unsigned int availWays, unsigned long linesCount, CacheLine<64>** lines) {
	CacheLineAllocator<64> a = CacheLineAllocator<64>(LLC, availWays);
	a.allocateSet(set, linesCount, lines);
}

class SetTester {
public:
	const unsigned int maxTestLinesCount;
	const unsigned long runs;
	const double anomalyFactor;

	CacheLine<64>** testLines;
	unsigned int testLinesCount;

	unsigned long median_sum;
	unsigned long median_count;
	double avgMed;

	SetTester(unsigned int maxTestLinesCount, unsigned long runs,
			double anomalyFactor) : maxTestLinesCount(maxTestLinesCount), runs(runs), anomalyFactor(anomalyFactor) {
		init();
	}

	SetTester(const SetTester& o) : maxTestLinesCount(o.maxTestLinesCount), runs(o.runs), anomalyFactor(o.anomalyFactor) {
		init();
		median_sum = o.median_sum;
		median_count = o.median_count;
		avgMed = o.avgMed;
	}

	~SetTester() {
		delete[] testLines;
	}

	void init() {
		testLines = new CacheLine<64>*[maxTestLinesCount];
		clear();
		median_sum = 0;
		median_count = 0;
		avgMed = 0;
	}

	void clear() {
		testLinesCount = 0;
	}

	void add(CacheLine<64>* line) {
		testLines[testLinesCount++] = line;
	}

	void add(CacheLine<64>** lines, unsigned int count) {
		for(unsigned int i=0; i<count; ++i) {
			add(lines[i]);
		}
	}

	void add(CacheLine<64>::vec& lines, unsigned int count) {
		for(unsigned int i=0; i<count; ++i) {
			add(lines[i]);
		}
	}

	int time() {
		return time_lines(testLines, testLinesCount, runs);
	}

	void warmupRun() {
		int median = time();
		median_sum += median;
		median_count += 1;
		avgMed = (double) median_sum / (double) median_count;
	}

	bool isOnSameSet() {
		// If time higher then avg., then they on the same set
		return ((double)time()) > (avgMed * anomalyFactor);
	}

	CacheLine<64>::vec getSameSetGroup() {
		CacheLine<64>::vec res;

		if(!isOnSameSet()) {
			return res;
		}

		SetTester subTester(*this);

		// For each u: if without u the access time is short, then it is part of the set
		for(unsigned int u=0; u < testLinesCount; ++u) {
			subTester.clear();

			for(unsigned int v=0; v < testLinesCount; ++v) {
				if(v==u) continue;
				subTester.add(testLines[v]);
			}

			if(!subTester.isOnSameSet()) {
				res.push_back(testLines[u]);
			}
		}

		return res;
	}
};

void benchmark(long set) {
	const unsigned int slicesCount = 12;

	const unsigned int availWays = 2;
	const unsigned int extraSearch = 2;

	const unsigned long linesCount = 24576;
	const unsigned int runs = 50000;
	const double anomalyFactor = 2.;

	const unsigned int maxTestLinesCount = availWays + extraSearch + 1;
	SetTester tester(maxTestLinesCount, runs, anomalyFactor);

	CacheLine<64>::vec sliceLines[slicesCount];

	unsigned int iter = 0;

	////////////////////////////////////////////////////////////////////////
	// Allocation
	////////////////////////////////////////////////////////////////////////
	CacheLine<64>** lines = new CacheLine<64>*[linesCount];
	allocateLines(set, availWays, linesCount, lines);
	std::cout << endl << "Total allocation poll: " << ((double)CacheLine<64>::getTotalAllocatedPoll() / (double)(1<<30)) << " GB" << endl;

	////////////////////////////////////////////////////////////////////////
	// Warmup
	////////////////////////////////////////////////////////////////////////
	unsigned int warmupLoops = linesCount - availWays;
	for(unsigned int i=0; i<warmupLoops; ++i) {
		tester.clear();
		tester.add(lines+i, availWays);
		tester.warmupRun();
	}

	std::cout << "Normal runtime: " << tester.avgMed << endl;

	////////////////////////////////////////////////////////////////////////
	// Testing
	////////////////////////////////////////////////////////////////////////
	for(unsigned int curSlice=0; curSlice < slicesCount; curSlice++) {
		std::cout << endl << "[SEARCHING LINE] slice: " << curSlice << endl;
		CacheLine<64>* baseline = NULL;

		// Find line not in existing slice
		for(unsigned int lineIter=0; lineIter < linesCount; lineIter++) {
			bool inExistingSlice = false;

			for(unsigned int u=0; u < curSlice; ++u) {
				if(find(sliceLines[u].begin(), sliceLines[u].end(), lines[lineIter]) != sliceLines[u].end()) {
					inExistingSlice = true;
					break;
				}

				tester.clear();
				tester.add(lines[lineIter]);
				tester.add(sliceLines[u], availWays);

				if(tester.isOnSameSet()) {
					inExistingSlice = true;
					break;
				}
			}

			if(inExistingSlice) {
				continue;
			} else {
				std::cout << "For slice: " << curSlice << " used line: " << lineIter << endl;
				baseline = lines[lineIter];
				break;
			}
		}

		if(baseline == NULL) {
			std::cout << "[FAILED]" << endl;
			throw CacheLineException("Could not find a line in this slice");
		} else {
			std::cout << "[SUCCESS]" << endl;
		}

		for(unsigned int lineIter=0; lineIter < linesCount-maxTestLinesCount; ++lineIter) {
			// Most of the times, the lines won't be in the same set (short access time)
			tester.clear();
			tester.add(baseline);
			tester.add(lines+lineIter, maxTestLinesCount-1);

			auto sameSet = tester.getSameSetGroup();
			if(sameSet.size() < availWays || find (sameSet.begin(), sameSet.end(), baseline) == sameSet.end()) {
				continue;
			}

			sliceLines[curSlice] = sameSet;
		}

		if (sliceLines[curSlice].size() < availWays) {
			throw CacheLineException("Did not found a set");
		}

		std::cout << "[SEARCHING ENTIRE SET] slice: " << curSlice << " (known-set-size: " << sliceLines[curSlice].size() << ")" << endl;
		for(unsigned int lineIter=0; lineIter < linesCount; lineIter++) {
			tester.clear();
			tester.add(lines[lineIter]);
			tester.add(sliceLines[curSlice], availWays);

			if(tester.isOnSameSet()) {
				sliceLines[curSlice].push_back(lines[lineIter]);
			}

			iter = (iter + 1) % 128;
			if(iter == 0) {
				std::cout.imbue(std::locale(""));
				std::cout << "\rTesting line: " << lineIter;
			}
		}

		double sameSetRatio = (double)sliceLines[curSlice].size() / (double)linesCount;
		cout << endl << "Total with same set: " << sliceLines[curSlice].size() << " - ratio: " << sameSetRatio << endl;

		ofstream outputfile;
		char filename[256];
		sprintf(filename, "../result/set-%lx-slice-%u-%llu.txt", set, curSlice, (rdtsc()*rdtsc()) % 10007);
		outputfile.open(filename);
		for(unsigned long i=0; i < sliceLines[curSlice].size(); ++i) {
			outputfile << std::hex << sliceLines[curSlice][i]->getPhysicalAddr() << endl;
		}
		outputfile.close();

		std::cout << "Saved to file" << filename << endl << endl;
	}
}
