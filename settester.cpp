/*
 * settester.cpp
 *
 *  Created on: Sep 2, 2015
 *      Author: liran
 */

#include "timing.h"
#include "settester.hpp"
#include "ObjectPoll.h"

CacheLine::arr SetTester::getRandomArray() {
	auto i = rand() % TEST_LINES_ARRAYS;
	if(testLinesArrays[i] == NULL) {
		testLinesArrays[i] = new CacheLine::ptr[maxTestLinesCount];
	}
	return testLinesArrays[i];
}

void SetTester::clearArrays() {
	for(unsigned int i=0; i < TEST_LINES_ARRAYS; ++i) {
		if(testLinesArrays[i] != NULL) {
			delete[] testLinesArrays[i];
		}
	}
}

#define DUMMY_SIZE ( (1<<12)/sizeof(int) )
int g_dummy[DUMMY_SIZE];

// Measure the time taken to access the given address, in nanoseconds.
inline int time_access(void* ptr, unsigned int loc)
		__attribute__((always_inline));
inline int time_access(void* ptr, unsigned int loc) {
//	struct timespec ts0;
//	clock_gettime(CLOCK_MONOTONIC, &ts0-Winline);

	unsigned long long start = rdtsc(), end;
	g_dummy[loc] += *(volatile int *) ptr;
	mfence();
	end = rdtsc();
	return end - start;

//	struct timespec ts;
//	clock_gettime(CLOCK_MONOTONIC, &ts);-Winline
//	return (int)((ts.tv_sec - ts0.tv_sec) * 1000000000 + (ts.tv_nsec - ts0.tv_nsec));
}

inline void clearLines(CacheLine::arr lines, unsigned long count)
		__attribute__((always_inline));

inline void clearLines(CacheLine::arr lines, unsigned long count) {
	for (unsigned int i = 0; i < count; i++) {
		lines[i]->flushFromCache();
	}
	mfence();
}

inline int time_last_access(CacheLine::arr lines, unsigned long size, unsigned int loc)
		__attribute__((always_inline));
inline int time_last_access(CacheLine::arr lines, unsigned long size, unsigned int loc) {
	clearLines(lines, size);

	// Ensure the first address is cached by accessing it.
	g_dummy[loc] += *(volatile int *) lines[0];
	mfence();
	// Now pull the other addresses through the cache too.
	for (unsigned int i = 1; i < size; i++) {
		g_dummy[loc] += *(volatile int *) lines[i];
	}
	mfence();
	// See whether the first address got evicted from the cache by
	// timing accessing it.
	return time_access(lines[0], loc);
}

inline int time_miss_access(CacheLine::ptr line, unsigned int loc)
		__attribute__((always_inline));
inline int time_miss_access(CacheLine::ptr line, unsigned int loc) {
	line->flushFromCache();
	mfence();
	return time_access(line,loc);
}

inline int time_line_miss_access(CacheLine::ptr line, unsigned long runs) __attribute__((always_inline));
inline int time_line_miss_access(CacheLine::ptr line, unsigned long runs) {
	int times[runs];
	unsigned int loc = rand() % DUMMY_SIZE;

	iopl(3);
	__asm__ __volatile__("cli");
	for (unsigned long run = 0; run < runs; run++) {
		times[run] = time_miss_access(line, loc);
	}
	__asm__ __volatile__("sti");

	return *std::min_element(times, times + runs);
}

inline void time_lines_safe(CacheLine::arr lines, unsigned long size, unsigned int loc,
		int* times, unsigned long runs)  __attribute__((always_inline));
inline void time_lines_safe(CacheLine::arr lines, unsigned long size, unsigned int loc,
		int* times, unsigned long runs) {
	iopl(3);
	__asm__ __volatile__("cli");
	for (unsigned long run = 0; run < runs; run++) {
		times[run] = time_last_access(lines, size, loc);
	}
	__asm__ __volatile__("sti");
}

inline int time_lines(CacheLine::arr lines, unsigned long size,
		unsigned long runs) __attribute__((always_inline));

inline int time_lines(CacheLine::arr lines, unsigned long size,
		unsigned long runs) {
	int times[runs];
	unsigned int loc = rand() % DUMMY_SIZE;

	clearLines(lines, size);
	time_lines_safe(lines, size, loc, times, runs);

	// Find the median time.  We use the median in order to discard
	// outliers.  We want to discard outlying slow results which are
	// likely to be the result of other activity on the machine.
	//
	// We also want to discard outliers where memory was accessed
	// unusually quickly.  These could be the result of the CPU's
	// eviction policy not using an exact LRU policy.
	std::sort(times, &times[runs]);
	int median_time = times[runs / 2];

	return median_time;
}

inline bool isOnSameSetAsTheFirst(CacheLine::arr lines, unsigned long size,
		unsigned long runs, int llcMaxAccessTime) __attribute__((always_inline));

inline bool isOnSameSetAsTheFirst(CacheLine::arr lines, unsigned long size,
		unsigned long runs, int llcMaxAccessTime) {
	int times[runs];
	unsigned int loc = rand() % DUMMY_SIZE;

//	clearLines(lines, size);

	unsigned int higherCount = 0;

	const double highSafeFactor = 0.75;
	const double lowSafeFactor = 1. - highSafeFactor;
	const unsigned int maxRetries = 16;
	unsigned long totalRuns = 0;

	for(unsigned int i=0; i < maxRetries; i++) {
		time_lines_safe(lines, size, loc, times, runs);
		totalRuns += runs;
		for (unsigned long run = 0; run < runs; run++) {
			if(times[run] > llcMaxAccessTime) {
				higherCount += 1;
			}
		}

		if(higherCount > totalRuns*highSafeFactor) {
			return true;
		} else if(higherCount < totalRuns*lowSafeFactor) {
			return false;
		}
	}

	// Fallback. Better safe then sorry.
	return false;
}

bool SetTester::isOnSameSet(unsigned int count) {
	switch(rand() % 3) {
	case 0:
		return isOnSameSetAsTheFirst(testLines, count, runs, llcMaxAccessTime);
	case 1:
		return isOnSameSetAsTheFirst(testLines, count, runs+2, llcMaxAccessTime);
	case 2:
	default:
		return isOnSameSetAsTheFirst(testLines, count, runs+4, llcMaxAccessTime);
	}
//	return double(time(count)) > avgMed * anomalyFactor;
}

int SetTester::time(unsigned int count) {
	return time_lines(testLines, count, runs);
}

int SetTester::timeMiss(CacheLine::ptr line) {
	return time_line_miss_access(line, runs);
}

CacheLine::vec SetTester::getSameSetGroup(unsigned int availableWays) {
	CacheLine::vec res;

	if(!isOnSameSet()) {
		return res;
	}

	// We only measure the first line access so,
	// it must be in the same set as some of the others
	unsigned int foundCount = 1;

	// If the amount of tested lines is exactly one more then the available ways,
	// then we know that all the group is in the same set, not need to check again.
	if(testLinesCount == availableWays+1) {
		foundCount = availableWays;
	} else {
		// For each u: if without u the access time is short, then it is part of the set
		for(unsigned int u=testLinesCount-1; u >= foundCount && foundCount < availableWays; u--) {
			if(!isOnSameSet(u)) {
				swap(foundCount, u);
				foundCount++;
				u++;
			}
		}
	}

	if(foundCount >= availableWays) {
		res.insert(res.begin(), testLines, testLines + foundCount);
	}

	return res;
}
