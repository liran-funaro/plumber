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
	const double anomalyFactor;

	unsigned long runs;
	unsigned int maxTestLinesCount;

	CacheLine::arr testLines;
	unsigned int testLinesCount;

	unsigned long median_sum;
	unsigned long median_count;
	double avgMed;

	SetTester() : baseRuns(64), anomalyFactor(1.6),
			runs(baseRuns), maxTestLinesCount(0),
			testLines(NULL), testLinesCount(0), median_sum(0), median_count(0), avgMed(0) {
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
		median_sum = 0;
		median_count = 0;
		avgMed = 0;
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

	int time() {
		return time(testLinesCount);
	}

	int time(unsigned int count);
	int time1(unsigned int count);
	int time2(unsigned int count);
	int time3(unsigned int count);

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
		return isOnSameSet(testLinesCount);
	}

	bool isOnSameSet(unsigned int count) {
		// If time higher then avg., then they on the same set
		return ((double)time(count)) > (avgMed * anomalyFactor);
	}

	bool isOnSameSet(CacheLine::ptr line) {
		testLines[testLinesCount++] = testLines[0];
		testLines[0] = line;
		auto ret = isOnSameSet();
		testLines[0] = testLines[--testLinesCount];
		return ret;
	}

	void swap(unsigned int u, unsigned int v) {
		CacheLine::ptr tmp = testLines[u];
		testLines[u] = testLines[v];
		testLines[v] = tmp;
	}

	CacheLine::vec getSameSetGroup(unsigned int size) {
		CacheLine::vec res;

		if(!isOnSameSet()) {
			return res;
		}

		// We only measure the first line access so,
		// it must be in the same set as some of the others
		unsigned int foundCount = 1;

		// For each u: if without u the access time is short, then it is part of the set
		for(unsigned int u=testLinesCount-1; u >= foundCount && foundCount < size; u--) {
			if(!isOnSameSet(u)) {
				swap(foundCount++, u);
			}
		}

		if(foundCount >= size) {
			res.insert(res.begin(), testLines, testLines + foundCount);
		}

		return res;
	}
};

#endif /* PLUMBER_SETTESTER_HPP_ */
