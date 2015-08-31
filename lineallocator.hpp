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
	const char* _what;
public:
	LineAllocatorException(const char* what) : _what(what) {}
	virtual const char* what() const throw (){return _what;}
};

template <unsigned int LINE_SIZE>
class CacheLineAllocator {
public:
	using CacheSets = map<unsigned int, CacheLine<64>::uset>;

private:
	const int cacheLevel;

	CacheInfo cacheInfo;

	unsigned int ways;
	unsigned int sets;
	unsigned int setsPerSlice;

	unsigned int linesPerSet;
	unsigned long availableWays;

	bool verbose;

	CacheSets linesSets;

public:
	CacheLineAllocator(int cacheLevel, unsigned int linesPerSet = 0,
			unsigned long availableWays = 2, bool verbose = false) : cacheLevel(cacheLevel), linesPerSet(linesPerSet),
			availableWays(availableWays), verbose(verbose) {
		cacheInfo = CacheInfo::getCacheLevel(cacheLevel);
		if(verbose) {
			cacheInfo.print();
		}

		if (cacheInfo.coherency_line_size != LINE_SIZE) {
			throw LineAllocatorException("Cache line size must be LINE_SIZE bytes");
		}

		ways = cacheInfo.ways_of_associativity;
		sets = cacheInfo.sets;
		setsPerSlice = sets / cacheInfo.cache_slices;

		if (linesPerSet == 0) {
			linesPerSet = ways;
		}

		lastFilename[0] = 0;
	}

	~CacheLineAllocator() {
		clean(0);
	}

private:
	CacheLine<LINE_SIZE>* newLine() const {
		return new CacheLine<LINE_SIZE>(setsPerSlice);
	}

	void allocateLine() {
		putLine(newLine());
	}

	void putLine(typename CacheLine<LINE_SIZE>::ptr line) {
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
			linesSizeSum += linesSets[i].size() * LINE_SIZE;
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
				CacheLine<LINE_SIZE>* line = *lineIt;
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

	const typename CacheLine<LINE_SIZE>::uset& allocateSet(unsigned long set, unsigned long count) {
		while(linesSets[set].size() < count) {
			allocateLine();
		}

		CacheLine<LINE_SIZE>::GC();

		return getSet(set);
	}

	const typename CacheLine<LINE_SIZE>::uset& getSet(unsigned long set) {
		return linesSets[set];
	}

	typename CacheLine<LINE_SIZE>::ptr getSet(int set, typename CacheLine<LINE_SIZE>::arr setLines, unsigned int count) {
		auto curSet = getSet(set);
		unsigned int pos = 0;
		typename CacheLine<LINE_SIZE>::ptr res = NULL;
		typename CacheLine<LINE_SIZE>::ptr curline = NULL;

		for (auto l = curSet.begin(); l != curSet.end() && pos < count; l++){
			if(setLines != NULL) {
				setLines[pos] = *l;
			}

			if(res == NULL) {
				res = *l;
				curline = *l;
			} else {
				curline->setNext(*l);
				curline = *l;
			}

			pos += 1;
		}

		curline->setNext(NULL);

		if (pos < count) {
			throw LineAllocatorException("Not enough lines in set");
		}

		return res;
	}

	typename CacheLine<LINE_SIZE>::ptr getAllSets(typename CacheLine<LINE_SIZE>::arr setLines, unsigned int countPerSet) {
		typename CacheLine<LINE_SIZE>::ptr res = NULL;
		typename CacheLine<LINE_SIZE>::ptr curline = NULL;

		for(unsigned int set=0; set < getSetsCount(); set++) {
			typename CacheLine<LINE_SIZE>::arr setLinesPos = setLines != NULL ? setLines + (countPerSet*set) : NULL;

			auto cursetline = getSet(set, setLinesPos, countPerSet);
			if(res == NULL) {
				res = cursetline;
				curline = cursetline;
			} else {
				curline->setNext(cursetline);
				curline = cursetline;
			}
		}

		return res;
	}

	void rePartitionSets() {
		auto oldMap = linesSets;
		linesSets.clear();
		for(auto setIt=oldMap.begin(); setIt != oldMap.end(); setIt++) {
			for(auto lineIt=setIt->second.begin(); lineIt != setIt->second.end(); lineIt++) {
				putLine(*lineIt);
			}
		}
	}

	void allocateAllSets() {
		for(unsigned int curSet=0; curSet < setsPerSlice; ++curSet) {
			std::cout << "[SET: " << setfill(' ') << setw(3) << dec << curSet << "] " << std::flush;

			auto& setLines = getSet(curSet);
			auto detector = CacheSliceDetector(setLines, cacheInfo.cache_slices, availableWays, linesPerSet, verbose);

			bool moreWork = true;
			bool moreLines = setLines.size() < linesPerSet;
			bool reset = false;
			bool doubleRuns = false;

			unsigned int allocationRetries = 0;
			unsigned int maxRetries = 10;

			while(moreWork) {
				if(moreLines) {
					if(verbose) {std::cout << "[ALLOCATION] Set: " << curSet << " " << std::flush;}
					else {std::cout << "Allocating, " << std::flush;}

					allocateSet(curSet, setLines.size() + linesPerSet);
					allocationRetries += 1;

					if(verbose) {
						std::cout << "[SUCCESS] Total: " << setLines.size() << " lines ("<< ((double)CacheLine<LINE_SIZE>::getTotalAllocatedPoll() / (double)(1<<30)) << " GB)" << endl;
					}

					moreLines = false;
				}
				if(reset) {
					if(verbose) {std::cout << "[RESET]" << endl;}
					else {std::cout << "Reset, " << std::flush;}
					detector.reset();
					reset = false;
				}
				if(doubleRuns) {
					if(verbose) {std::cout << "[DOUBLE RUNS]" << endl;}
					else {std::cout << "Double-runs, " << std::flush;}
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

						reset = true;
						doubleRuns = true;
						allocationRetries = 0;
					}
				} catch (CacheSliceResetException& e) {
					if(verbose) {
						std::cout << endl << "[ERROR] Set: " << dec << curSet << " - " << e.what() << " => " << std::flush;
					}
					moreWork = true;
					moreLines = false;
					reset = true;
					doubleRuns = true;

					CacheLine<64>* line = const_cast<CacheLine<64>*>((CacheLine<64>*)e.line());
					linesSets[curSet].erase(line);
					delete line;
				} catch (CacheLineException& e) {
					if(verbose) {
						std::cout << endl << "[ERROR] Set: " << dec << curSet << " - " << e.what() << " => " << std::flush;
					} else {
						std::cout << e.what() << ", " << std::flush;
					}
					moreWork = true;
					moreLines = false;
					reset = true;
					doubleRuns = true;
				}
			}

			std::cout << "[SUCCESS SET: " << setfill(' ') << setw(3) << dec << curSet << "] " << endl;
		}

		rePartitionSets();
		write();
	}

	void detectNewSets(unsigned long curSet) {
		auto& setLines = getSet(curSet);
		auto detector = CacheSliceDetector(setLines, cacheInfo.cache_slices, availableWays);
		detector.findAllLinesOnExistingSets();
	}

	void detectNewSets() {
		for(unsigned int curSet=0; curSet < setsPerSlice; ++curSet) {
			detectNewSets(curSet);
		}
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

	char lastFilename[512];

	void write() {
		char filename[512];
		sprintf(filename, "../result/lineallocator-%llu.txt", rdtsc());
		ofstream outputfile;
		outputfile.open(filename);
		outputfile << "#SET;SLICE;ADDR" << endl;

		for(auto s = linesSets.begin(); s != linesSets.end(); s++) {
			auto& curSliceSet = s->second;

			for(auto i=curSliceSet.begin(); i != curSliceSet.end(); ++i) {
				outputfile << std::hex
						<< (*i)->getSet() << ";"
						<< (*i)->getCacheSlice() << ";"
						<< (*i)->getPhysicalAddr() << endl;
			}
		}

		outputfile.close();
		std::cout << "[SAVE] Saved to file" << filename << endl;

		if(lastFilename[0] != 0) {
			remove(lastFilename);
		}

		strcpy(lastFilename, filename);
	}
};

#endif /* PLUMBER_LINEALLOCATOR_HPP_ */
