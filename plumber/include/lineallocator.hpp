/*
 * Author: Liran Funaro <liran.funaro@gmail.com>
 *
 * Copyright (C) 2006-2018 Liran Funaro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef PLUMBER_LINEALLOCATOR_HPP_
#define PLUMBER_LINEALLOCATOR_HPP_

#include <stddef.h>
#include <iostream>
#include <map>
#include <set>
#include <utility>

#include "cacheline.hpp"
#include "cpuid_cache.h"
#include "slicedetector.hpp"
#include "plumber.hpp"

using namespace std;

class LineAllocatorException: public PlumberException { using PlumberException::PlumberException; };

class CacheLineAllocator {
public:
	using CacheSets = map<unsigned int, CacheLine::uset>;

private:
	const int cacheLevel;

	CacheInfo cacheInfo;

	unsigned int lineSize;
	unsigned int ways;
	unsigned int sets;
	unsigned int setsPerSlice;

	unsigned int linesPerSet;
	unsigned long availableWays;

	bool verbose;
	bool printAllocationInformation;

	CacheSets linesSets;
	CacheSliceDetector detector;

	char lastFilename[512];

public:
	CacheLineAllocator(int cacheLevel, unsigned int inputLinesPerSet = 0,
			unsigned long availableWays = 2, bool verbose = false) : cacheLevel(cacheLevel), linesPerSet(inputLinesPerSet),
			availableWays(availableWays), verbose(verbose), detector(verbose) {
		cacheInfo = CacheInfo::getCacheLevel(cacheLevel);
		if(verbose) {
			cacheInfo.print();
		}

		printAllocationInformation = true;

		lineSize = cacheInfo.coherency_line_size;
		ways = cacheInfo.ways_of_associativity;
		sets = cacheInfo.sets;
		setsPerSlice = sets / cacheInfo.cache_slices;

		if (linesPerSet == 0) {
			linesPerSet = ways;
		}

		lastFilename[0] = 0;

		CacheLine::allocatePoll(lineSize);
	}

	~CacheLineAllocator() {
		clean(0);
	}

public:
	unsigned int getLinesPerSet() const { return linesPerSet; }
	unsigned int getSetsCount() const { return sets; }
	unsigned int getWaysCount() const { return ways; }
	const CacheLine::uset& getSet(unsigned long set) { return linesSets[set]; }

private:
	CacheLine::ptr newLine() const { return new CacheLine(lineSize, setsPerSlice); }

	void allocateLine() { putLine(newLine()); }

	void discardLine(CacheLine::ptr line) {
		auto curSet = line->getInSliceSet();
		linesSets[curSet].erase(line);

		curSet = line->getSet();
		linesSets[curSet].erase(line);

		delete line;
	}

	void putLine(CacheLine::ptr line) {
		unsigned long lineSet = line->getSet();
		linesSets[lineSet].insert(line);
	}

	bool isSetFull(unsigned long set) {
		return linesSets[set].size() < linesPerSet;
	}

	bool isAllGroupsFull() {
		for (unsigned long i = 0; i < sets; i++) {
			if (isSetFull(i)) {
				return false;
			}
		}

		return true;
	}

	unsigned long getLinesSizeSum() {
		unsigned long linesSizeSum = 0;
		for (unsigned long i = 0; i < sets; i++) {
			linesSizeSum += linesSets[i].size() * lineSize;
		}

		return linesSizeSum;
	}

	void clean() { clean(ways);	}
	void clean(unsigned int maxElementsInGroup);

public:

	CacheLine::lst getSet(int set, unsigned int count);
	CacheLine::lst getSets(unsigned int beginSet, unsigned int endSet, unsigned int countPerSet);
	CacheLine::lst getAllSets(unsigned int countPerSet) {
		return getSets(0, getSetsCount(), countPerSet);
	}

	void allocateAllSets();
	void rePartitionSets();
	const CacheLine::uset& allocateSet(unsigned long set, unsigned long count);

	void print() const;
	void write(const char* path);
};

#endif /* PLUMBER_LINEALLOCATOR_HPP_ */
