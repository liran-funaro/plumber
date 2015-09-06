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

class SetTester {
	enum { TEST_LINES_ARRAYS = 7 };
	CacheLine::arr testLinesArrays[TEST_LINES_ARRAYS];

public:
	const unsigned long baseRuns;

	unsigned long runs;
	unsigned int maxTestLinesCount;

	CacheLine::arr testLines;
	unsigned int testLinesCount;

	unsigned long hitMedianSum;
	unsigned long hitMedianCount;
	double avgHitAccessTime;

	unsigned long missMedianSum;
	unsigned long missMedianCount;
	double avgMissAccessTime;
	unsigned int llcMaxAccessTime;

	SetTester() : baseRuns(16),
			runs(baseRuns), maxTestLinesCount(0),
			testLines(NULL), testLinesCount(0),
			hitMedianSum(0), hitMedianCount(0),	avgHitAccessTime(0),
			missMedianSum(0), missMedianCount(0), avgMissAccessTime(0),
			llcMaxAccessTime(0) {
		for (int i = 0; i < TEST_LINES_ARRAYS; ++i) {
			testLinesArrays[i] = NULL;
		}
	}

	~SetTester() {
		clearArrays();
	}

	void doubleRuns() {
		runs += baseRuns;
	}

	void restartRuns() {
		runs = baseRuns;
	}

	void init(unsigned int maxTestLinesCount) {
		this->maxTestLinesCount = maxTestLinesCount;
		clearArrays();
		clear();
		hitMedianSum = 0;
		hitMedianCount = 0;
		avgHitAccessTime = 0;
		missMedianSum = 0;
		missMedianCount = 0;
		avgMissAccessTime = 0;
		llcMaxAccessTime = 0;
	}

	CacheLine::arr getRandomArray();
	void clearArrays();

	void clear() {
		testLinesCount = 0;
		testLines = getRandomArray();
	}

	void add(const CacheLine::ptr line) {
		testLines[testLinesCount++] = line;
	}

	void add(CacheLine::arr lines, unsigned int count) {
		for(unsigned int i=0; i<count; ++i) {
			add(lines[i]);
		}
	}

	void add(const CacheLine::vec& lines, unsigned int count) {
		for(unsigned int i=0; i<count; ++i) {
			add(lines[i]);
		}
	}

	void add(const CacheLine::vec& lines, unsigned int from, unsigned int count) {
		for(unsigned int i=0; i<count; ++i) {
			add(lines[from+i]);
		}
	}

	void add(const CacheLine::uset& lines, unsigned int count) {
		for(auto i = lines.begin(); count > 0 && i != lines.end(); i++) {
			add(*i);
			count -= 1;
		}
	}

	void addRandom(CacheLine::vec& lines, unsigned int count) {
		auto len = lines.size();

		while(count > 0) {
			unsigned int index = rand() % len;
			add(lines[index]);
			len -= 1;
			auto tmp = lines[len];
			lines[len] = lines[index];
			lines[index] = tmp;
			count -= 1;
		}
	}

	void removeLast() {
		if(testLinesCount > 0) {
			testLinesCount -= 1;
		}
	}

	bool isInTestedLines(CacheLine::ptr line) {
		for(unsigned int i=0; i < testLinesCount; ++i) {
			if(testLines[i] == line) {
				return true;
			}
		}

		return false;
	}

	int time(unsigned int count);
	int time() { return time(testLinesCount); }

	bool isOnSameSet(unsigned int count);
	bool isOnSameSet() { return isOnSameSet(testLinesCount); }

	bool isOnSameSet(CacheLine::ptr line) {
		testLines[testLinesCount++] = testLines[0];
		testLines[0] = line;
		auto ret = isOnSameSet();
		testLines[0] = testLines[--testLinesCount];
		return ret;
	}

	int timeMiss(CacheLine::ptr line);
	void warmupRun() {
		if(testLinesCount == 0) {
			return;
		}

		for(unsigned int i=0; i < testLinesCount; i++) {
			hitMedianSum += time();
			hitMedianCount += 1;
			avgHitAccessTime = (double) hitMedianSum / (double) hitMedianCount;
		}

		for(unsigned int i=0; i < testLinesCount; i++) {
			missMedianSum += timeMiss(testLines[i]);
			missMedianCount += 1;
			avgMissAccessTime = (double) missMedianSum / (double) missMedianCount;
		}

		llcMaxAccessTime = avgHitAccessTime*0.85 + avgMissAccessTime*0.15;
	}

	void swap(unsigned int u, unsigned int v) {
		CacheLine::ptr tmp = testLines[u];
		testLines[u] = testLines[v];
		testLines[v] = tmp;
	}

	CacheLine::vec getSameSetGroup(unsigned int availableWays);
};

#endif /* PLUMBER_SETTESTER_HPP_ */
