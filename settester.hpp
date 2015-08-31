/*
 * settester.hpp
 *
 *  Created on: Aug 27, 2015
 *      Author: liran
 */

#ifndef PLUMBER_SETTESTER_HPP_
#define PLUMBER_SETTESTER_HPP_

#include <algorithm>
#include <cstdlib>
#include <sys/io.h>

#include "cacheline.hpp"
#include "timing.h"

int g_dummy[1<<12];

// Measure the time taken to access the given address, in nanoseconds.
inline int time_access(void* ptr, unsigned int loc) __attribute__((always_inline));
inline int time_access(void* ptr, unsigned int loc) {
//	struct timespec ts0;
//	clock_gettime(CLOCK_MONOTONIC, &ts0-Winline);

	unsigned long long start = rdtsc(), end;
	g_dummy[loc] += *(volatile int *) ptr;
	mfence();
	end=rdtsc();
	return end-start;

//	struct timespec ts;
//	clock_gettime(CLOCK_MONOTONIC, &ts);-Winline
//	return (int)((ts.tv_sec - ts0.tv_sec) * 1000000000 + (ts.tv_nsec - ts0.tv_nsec));
}

inline void clearLines(CacheLine<64>::arr lines, unsigned long count) __attribute__((always_inline));
inline void clearLines(CacheLine<64>::arr lines, unsigned long count) {
	for(unsigned int i=0; i < count; i++) {
		lines[i]->flushFromCache();
	}
	mfence();
}

inline int time_lines(CacheLine<64>::arr lines, unsigned long numbersOfWays, unsigned long runs, unsigned int loc) __attribute__((always_inline));
inline int time_lines(CacheLine<64>::arr lines, unsigned long numbersOfWays, unsigned long runs, unsigned int loc) {
	int* times = new int[runs];

	clearLines(lines, numbersOfWays);

	const unsigned int last = numbersOfWays-1;

	iopl(3);
	__asm__ __volatile__("cli");
	for (unsigned long run = 0; run < runs; run++) {
		// Ensure the first address is cached by accessing it.
		g_dummy[loc] += *(volatile int *) lines[last];
		mfence();
		// Now pull the other addresses through the cache too.
		for (unsigned int i = 0; i < last; i++) {
			g_dummy[loc] += *(volatile int *) lines[i];
		}
		mfence();
		// See whether the first address got evicted from the cache by
		// timing accessing it.
		times[run] = time_access(lines[last], loc);
	}
	__asm__ __volatile__("sti");
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

template <unsigned int LINE_SIZE>
class SetTester {
	using lineClass = CacheLine<LINE_SIZE>;

public:
	const unsigned int maxTestLinesCount;
	const unsigned long baseRuns;
	unsigned long runs;
	const double anomalyFactor;

	typename lineClass::arr testLines;
	unsigned int testLinesCount;

	unsigned long median_sum;
	unsigned long median_count;
	double avgMed;
	bool verb;

	unsigned int timeFunc = 0;

	SetTester(unsigned int maxTestLinesCount, unsigned long runs,
			double anomalyFactor) : maxTestLinesCount(maxTestLinesCount), baseRuns(runs), runs(runs), anomalyFactor(anomalyFactor) {
		init();
	}

	SetTester(const SetTester& o) : maxTestLinesCount(o.maxTestLinesCount), baseRuns(o.baseRuns), runs(o.runs), anomalyFactor(o.anomalyFactor) {
		init();
		median_sum = o.median_sum;
		median_count = o.median_count;
		avgMed = o.avgMed;

		timeFunc = 0;
	}

	void doubleRuns() {
		runs += baseRuns;
		timeFunc = (timeFunc + 1) % 5;
	}

	~SetTester() {
		delete[] testLines;
	}

	void init() {
		testLines = new typename lineClass::ptr[maxTestLinesCount];
		clear();
		median_sum = 0;
		median_count = 0;
		avgMed = 0;

		verb = false;
	}

	void clear() {
		testLinesCount = 0;
	}

	void add(typename lineClass::ptr line) {
		testLines[testLinesCount++] = line;
	}

	void add(typename lineClass::arr lines, unsigned int count) {
		for(unsigned int i=0; i<count; ++i) {
			add(lines[i]);
		}
	}

	void add(typename lineClass::vec& lines, unsigned int count) {
		for(unsigned int i=0; i<count; ++i) {
			add(lines[i]);
		}
	}

	void add(typename lineClass::vec& lines, unsigned int from, unsigned int count) {
		for(unsigned int i=0; i<count; ++i) {
			add(lines[from+i]);
		}
	}

	void add(typename lineClass::uset& lines, unsigned int count) {
		for(auto i = lines.begin(); count > 0 && i != lines.end(); i++) {
			add(*i);
			count -= 1;
		}
	}

	template<typename T>
	void add(T& lines,unsigned int from,  unsigned int count) {
		for(auto i = lines.begin(); count > 0 && i != lines.end(); i++) {
			if(from > 0) {
				i++;
				from--;
			} else {
				add(*i);
				count -= 1;
			}
		}
	}

	void addRandom(typename lineClass::vec& lines, unsigned int count) {
		typename lineClass::uset res;
		auto len = lines.size();

		while(res.size() < count) {
			unsigned int index = rand() % len;
			res.insert(lines[index]);
		}

		for(auto it=res.begin(); it != res.end(); it++) {
			add(*it);
		}
	}

	void removeLast() {
		if(testLinesCount > 0) {
			testLinesCount -= 1;
		}
	}

	bool isInTestedLines(typename lineClass::ptr line) {
		for(unsigned int i=0; i < testLinesCount; ++i) {
			if(testLines[i] == line) {
				return true;
			}
		}

		return false;
	}

	int time() {
		switch(timeFunc % 3) {
		case 0:	return time1();
		case 1:	return time2();
		case 2:	return time3();
		default: return time_lines(testLines, testLinesCount, runs+4, timeFunc * (LINE_SIZE/sizeof(int)));
		}
	}

	int time1() {
		return time_lines(testLines, testLinesCount, runs+1, timeFunc * (LINE_SIZE/sizeof(int)));
	}

	int time2() {
		return time_lines(testLines, testLinesCount, runs+2, timeFunc * (LINE_SIZE/sizeof(int)));
	}

	int time3() {
		return time_lines(testLines, testLinesCount, runs+3, timeFunc * (LINE_SIZE/sizeof(int)));
	}

	void warmupRun() {
		if(testLinesCount == 0) {
			return;
		}

		int median = time();
		median_sum += median;
		median_count += 1;
		avgMed = (double) median_sum / (double) median_count;
	}

	bool isOnSameSet() {
		// If time higher then avg., then they on the same set
		return ((double)time()) > (avgMed * anomalyFactor);
	}

	bool isOnSameSet(typename lineClass::ptr line) {
		add(line);
		auto ret = isOnSameSet();
		removeLast();
		return ret;
	}

	void setVerb(bool _verb = true) {
		verb = _verb;
	}

	typename lineClass::uset getSameSetGroup() {
		typename lineClass::uset res;

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
				res.insert(testLines[u]);
			}
		}

		// If for all the time was still long, then all must be in the same set
		if(res.size() == 0) {
			for(unsigned int v=0; v < testLinesCount; ++v) {
				res.insert(testLines[v]);
			}
		}

		return res;
	}
};



#endif /* PLUMBER_SETTESTER_HPP_ */
