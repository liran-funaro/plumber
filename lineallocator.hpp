/*
 * pageallocator.h
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
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

using namespace std;

class LineAllocatorException: public exception {
	const string _what;
public:
	LineAllocatorException(const char* what) : _what(what) {}
	LineAllocatorException(const string& what) : _what(what) {}
	LineAllocatorException(const stringstream& what) : _what(what.str()) {}
	virtual const char* what() const throw (){return _what.c_str();}
};

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

	char lastFilename[512];

public:
	CacheLineAllocator(int cacheLevel, unsigned int inputLinesPerSet = 0,
			unsigned long availableWays = 2, bool verbose = false) : cacheLevel(cacheLevel), linesPerSet(inputLinesPerSet),
			availableWays(availableWays), verbose(verbose) {
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
	unsigned int getLinesPerSet() const {
		return linesPerSet;
	}

private:
	CacheLine::ptr newLine() const {
		return new CacheLine(lineSize, setsPerSlice);
	}

	void allocateLine() {
		putLine(newLine());
	}

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

	void clean() {
		clean(ways);
	}

	void clean(unsigned int maxElementsInGroup) {
		if(verbose) {
			std::cout << endl << "Cleaning..." << endl;
		}
		for (auto setIt = linesSets.begin(); setIt != linesSets.end(); setIt++) {
			while (setIt->second.size() > maxElementsInGroup) {
				auto lineIt = setIt->second.begin();
				CacheLine::ptr line = *lineIt;
				setIt->second.erase(lineIt);
				delete line;
			}
		}
	}

public:
	unsigned int getSetsCount() const {
		return sets;
	}

	unsigned int getWaysCount() const {
		return ways;
	}

	const CacheLine::uset& allocateSet(unsigned long set, unsigned long count) {
		while(linesSets[set].size() < count) {
			allocateLine();
		}

		CacheLine::GC();

		return getSet(set);
	}

	const CacheLine::uset& getSet(unsigned long set) {
		return linesSets[set];
	}

	CacheLine::lst getSet(int set, unsigned int count) {
		auto curSet = getSet(set);
		CacheLine::lst ret;

		for (auto l = curSet.begin(); l != curSet.end() && ret.size() < count; l++) {
			ret.insertBack(*l);
		}

		if (ret.size() < count) {
			throw LineAllocatorException("Not enough lines in set");
		}

		auto length = validateUniqueLineList(ret.front());
		if(length != ret.size()) {
			throw LineAllocatorException("Actual list length does not match (per set)");
		}

		return ret;
	}

	CacheLine::lst getAllSets(unsigned int countPerSet) {
		CacheLine::lst ret;

		for(unsigned int set=0; set < getSetsCount(); set++) {
			auto setList = getSet(set, countPerSet);
			ret.insertBack(setList);
		}

		auto length = validateUniqueLineList(ret.front());
		if(length != ret.size()) {
			throw LineAllocatorException("Actual list length does not match (all sets)");
		}

		return ret;
	}

	unsigned long validateUniqueLineList(CacheLine::ptr lineList) {
		CacheLine::ptr curline = lineList;

		CacheLine::uset lineSet;
		unsigned long count = 0;


		while (curline != NULL) {
			lineSet.insert(curline);
			count += 1;
			curline = curline->next;
		}

		if(count != lineSet.size()) {
			stringstream ss;
			ss << "Repeating items in list. List length: " << count << " but " << lineSet.size() << " unique items";
			throw LineAllocatorException(ss);
		}

		curline->validateAll();

		return count;
	}

	void rePartitionSets() {
		auto oldMap = linesSets;
		linesSets.clear();
		for(auto setIt=oldMap.begin(); setIt != oldMap.end(); setIt++) {
			for(auto lineIt=setIt->second.begin(); lineIt != setIt->second.end(); lineIt++) {
				if((*lineIt)->getCacheSlice() < 0) {
					discardLine(*lineIt);
				} else {
					putLine(*lineIt);
				}
			}
		}
	}

	void allocateAllSets() {
		for(unsigned int curSet=0; curSet < setsPerSlice; ++curSet) {
			if(verbose || printAllocationInformation) {
				std::cout << "[SET: " << setfill(' ') << setw(5) << dec << curSet << "] " << std::flush;
			}

			auto& setLines = getSet(curSet);
			auto detector = CacheSliceDetector(setLines, cacheInfo.cache_slices, availableWays, linesPerSet, verbose);

			bool moreWork = true;
			bool moreLines = setLines.size() < linesPerSet;
			bool doubleRuns = false;

			unsigned int allocationRetries = 0;
			unsigned int maxRetries = 10;

			while(moreWork) {
				if(moreLines) {
					if(verbose) {std::cout << "[ALLOCATION] Set: " << curSet << " " << std::flush;}
					else if(printAllocationInformation) {std::cout << "Allocating, " << std::flush;}

					allocateSet(curSet, setLines.size() + linesPerSet);
					allocationRetries += 1;

					if(verbose) {
						std::cout << "[SUCCESS] Total: " << setLines.size() << " lines ("<< ((double)CacheLine::getTotalAllocatedPoll() / (double)(1<<30)) << " GB)" << endl;
					}

					moreLines = false;
				}
				if(doubleRuns) {
					if(verbose) {std::cout << "[DOUBLE RUNS]" << endl;}
					else if(printAllocationInformation) {std::cout << "Double-runs, " << std::flush;}
					detector.doubleRuns();
					doubleRuns = false;
				}

				try {
					detector.detectAllCacheSlices();
					moreWork = false;
				} catch (NeedMoreLinesException& e) {
					if(verbose) {
						std::cout << endl << "[ERROR] Set: " << dec <<curSet << " - " << e.what() << " => " << std::flush;
					}
					moreWork = true;
					moreLines = true;

					if(allocationRetries >= maxRetries) {
						bool error = false;
						for(auto l = setLines.begin(); l != setLines.end(); ++l) {
							bool addressCorrect = (*l)->getPhysicalAddr() == (*l)->calculatePhyscialAddr();
							if(!addressCorrect) {
								error = true;
								if(verbose) {
									std::cout << "   [CHANGED] 0x" << hex << (*l)->getPhysicalAddr() << " != 0x" << (*l)->calculatePhyscialAddr() << endl;
								}
							}
						}

						if (error){
							throw LineAllocatorException("Address changed");
						}

						doubleRuns = true;
						allocationRetries = 0;
					}
				} catch (CacheSliceResetException& e) {
					if(verbose) {
						std::cout.imbue(std::locale());
						std::cout << endl << "[ERROR] Set: " << dec << curSet << " - " << e.what()
								<< " -- for address: 0x" << hex << ((CacheLine::ptr)e.line())->getPhysicalAddr() << " => " << std::flush;
					}
					moreWork = true;
					moreLines = false;
					doubleRuns = true;

					discardLine((CacheLine*)e.line());
				} catch (CacheLineException& e) {
					if(verbose) {
						std::cout << endl << "[ERROR] Set: " << dec << curSet << " - " << e.what() << " => " << std::flush;
					} else if(printAllocationInformation) {
						std::cout << e.what() << ", " << std::flush;
					}
					moreWork = true;
					moreLines = false;
					doubleRuns = true;
				}
			}

			if(verbose || printAllocationInformation) {
				std::cout << "[SUCCESS SET: " << setfill(' ') << setw(5) << dec << curSet << "] " << endl;
			}
		}

		rePartitionSets();
		write();

		SetTester::clearArrays();
	}

	void print() const {
		for (auto setIt = linesSets.begin(); setIt != linesSets.end(); setIt++) {
			cout << "Group: " << dec << setIt->first << endl;
			for (auto it = setIt->second.begin(); it != setIt->second.end();
					it++) {
				(*it)->print();
			}
			cout << endl;
		}
		cout << endl;
	}

	void write();
};

#endif /* PLUMBER_LINEALLOCATOR_HPP_ */
