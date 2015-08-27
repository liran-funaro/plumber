/*
 * pageallocator.h
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

#ifndef PAGEALLOCATOR_H_
#define PAGEALLOCATOR_H_

#include <list>
#include <exception>

#include "cacheline.h"
#include "cpuid_cache.h"

using namespace std;

class WaysException: public exception {
	const char* _what;
public:
	WaysException(const char* what);
	virtual const char* what() const throw ();
};

template <unsigned int LINE_SIZE>
class CacheLineAllocator {
	int cacheLevel;
	unsigned long maxExtraSize;

	CacheInfo cacheInfo;

	unsigned int waysCount;
	unsigned int setsCount;

	list<CacheLine<64>*>* linesSetGroups;
	CacheLine<64>** waysLines;

	unsigned long linesSizeSum;

public:
	CacheLineAllocator(int cacheLevel, unsigned int waysToFill = 0,
			unsigned long maxExtraSize = 128 << 20) : cacheLevel(cacheLevel), maxExtraSize(maxExtraSize) {
		cacheInfo = CacheInfo::getCacheLevel(cacheLevel);
//		cacheInfo.print();

		if (cacheInfo.coherency_line_size != LINE_SIZE) {
			throw WaysException("Cache line size must be LINE_SIZE bytes");
		}

		if (waysToFill == 0) {
			waysCount = cacheInfo.ways_of_associativity;
		} else{
			waysCount = waysToFill;
		}

		setsCount = cacheInfo.sets;

		linesSetGroups = new list<CacheLine<LINE_SIZE>*>[setsCount];
		waysLines = new CacheLine<LINE_SIZE>*[waysCount];

		linesSizeSum = 0;
	}

	~CacheLineAllocator() {
		if (waysLines != NULL) {
			delete[] waysLines;
		}

		if (linesSetGroups != NULL) {
			clean(0);

			delete[] linesSetGroups;
		}
	}

private:
	CacheLine<LINE_SIZE>* newLine() const {
		return new CacheLine<LINE_SIZE>(setsCount);
	}

	void allocateLine() {
		CacheLine<LINE_SIZE>* line = newLine();
		unsigned long lineSet = line->getSet();

		linesSetGroups[lineSet].push_back(line);

		linesSizeSum += LINE_SIZE;

		if (linesSizeSum > cacheInfo.total_size + maxExtraSize) {
			clean();
			linesSizeSum = getLinesSizeSum();
		}
	}

	bool isSetFull(unsigned long set) {
		return linesSetGroups[set].size() < waysCount;
	}

	bool isAllGroupsFull() {
		if (linesSizeSum < cacheInfo.total_size) {
			return false;
		}

		for (unsigned long i = 0; i < setsCount; i++) {
			if (isSetFull(i)) {
				return false;
			}
		}

		return true;
	}

	unsigned long getLinesSizeSum() {
		unsigned long linesSizeSum = 0;
		for (unsigned long i = 0; i < setsCount; i++) {
			linesSizeSum += linesSetGroups[i].size() * LINE_SIZE;
		}

		return linesSizeSum;
	}

	void clean() {
		clean(waysCount);
	}

	void clean(unsigned int maxElementsInGroup) {
		std::cout << endl << "Cleaning..." << endl;
		for (unsigned long i = 0; i < setsCount; i++) {
			while (linesSetGroups[i].size() > maxElementsInGroup) {
				CacheLine<LINE_SIZE>* line = linesSetGroups[i].front();
				linesSetGroups[i].pop_front();
				delete line;
			}
		}
	}

public:
	unsigned int getSetsCount() const {
		return setsCount;
	}

	unsigned int getWaysCount() const {
		return waysCount;
	}

	void allocateSet(long set, unsigned long count, CacheLine<LINE_SIZE>** res) {
		static const unsigned long minAddr = 0x00000000000;
		static const unsigned long maxAddr = 0x00FFFFFFFFF;
		unsigned long currentCount = 0;

		unsigned int iter = 0;

		while(currentCount < count) {
			CacheLine<LINE_SIZE>* line = newLine();

			auto addr = line->getPhysicalAddr();
			if(addr > maxAddr || addr < minAddr) {
				delete line;
				continue;
			}

			if (currentCount == 0 && set < 0) {
				set = line->getSet();
			}

			if(line->getSet() == set) {
				res[currentCount++] = line;
			} else {
				delete line;
			}

//			if(currentCount == 1) {
//				CacheLine<64>::setPageOffset(res[0]);
//			}

			iter = (iter + 1) % 512;
			if(iter == 0) {
				std::cout.imbue(std::locale(""));
				std::cout << "\rAllocating lines: " << currentCount;
			}
		}
	}

	void allocateAll() {
		static unsigned int iter = 0;

		while (!isAllGroupsFull()) {
			for(unsigned int i=0; i < 1000; ++i) {
				allocateLine();
			}

			iter = (iter + 1) % 128;
			if (iter == 0) {
				std::cout.imbue(std::locale(""));
				std::cout << "\rAllocating lines: " << linesSizeSum;
			}
		}

		clean();

		for (unsigned int i = 0; i < waysCount; i++) {
			waysLines[i] = NULL;
		}

		for (unsigned long g = 0; g < setsCount; g++) {
			unsigned int i = 0;
			for (auto it = linesSetGroups[g].begin(); it != linesSetGroups[g].end();
					it++, i++) {
				(*it)->setNext(waysLines[i]);
				waysLines[i] = *it;
				(*it)->lock();
				(*it)->validatePhyscialAddr();
			}
		}
	}

	inline void unsafeTouchSet(CacheLine<LINE_SIZE>** ways, unsigned int waysCount) {
		for (unsigned long i = 0; i < waysCount; ++i) {
			ways[i] = ways[i]->line.data.next;
		}
	}

	inline void unsafeTouchWays(unsigned int way1, unsigned int waysCount) {
		CacheLine<LINE_SIZE>* curWays[waysCount];

		for (unsigned int i=0; i < waysCount; i++){
			curWays[i] = waysLines[way1+i];
		}

		while (curWays[0] != NULL) {
			unsafeTouchSet(curWays, waysCount);
		}
	}

	void touchWays(unsigned int way1, unsigned int way2) {
		if (way1 > way2 || way2 >= waysCount) {
			throw WaysException("Ways out of bound");
		}

		unsigned int waysCount = way2-way1+1;
		unsafeTouchWays(way1, waysCount);
	}

	void print() const {
		for (unsigned long i = 0; i < setsCount; i++) {
			cout << "Group: " << dec << i << endl;
			for (auto it = linesSetGroups[i].begin(); it != linesSetGroups[i].end();
					it++) {
				(*it)->print();
			}
			cout << endl;
		}
		cout << endl;
	}
};

#endif /* PAGEALLOCATOR_H_ */
