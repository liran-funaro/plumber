/*
 * settester.cpp
 *
 *  Created on: Sep 2, 2015
 *      Author: liran
 */

#include "timing.h"
#include "settester.hpp"
#include "ObjectPoll.h"

CacheLine::arr SetTester::testLinesArrays[TEST_LINES_ARRAYS] = {NULL};

CacheLine::arr SetTester::getRandomArray(unsigned int maxTestLinesCount) {
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

inline int time_lines(CacheLine::arr lines, unsigned long numbersOfWays,
		unsigned long runs) __attribute__((always_inline));

inline int time_lines(CacheLine::arr lines, unsigned long numbersOfWays,
		unsigned long runs) {
	int times[runs];
	unsigned int loc = rand() % DUMMY_SIZE;

	clearLines(lines, numbersOfWays);

	const unsigned int last = numbersOfWays - 1;

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

	return median_time;
}

int SetTester::time(unsigned int count) {
	switch (rand() % 3) {
	case 0:
		return time1(count);
	case 1:
		return time2(count);
	case 2:
		return time3(count);
	default:
		return time3(count);
	}
}

int SetTester::time1(unsigned int count) {
	return time_lines(testLines, count, runs + 1);
}

int SetTester::time2(unsigned int count) {
	return time_lines(testLines, count, runs + 2);
}

int SetTester::time3(unsigned int count) {
	return time_lines(testLines, count, runs + 3);
}
