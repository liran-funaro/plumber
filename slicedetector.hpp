/*
 * slicedetector.hpp
 *
 *  Created on: Aug 30, 2015
 *      Author: liran
 */

#ifndef PLUMBER_SLICEDETECTOR_HPP_
#define PLUMBER_SLICEDETECTOR_HPP_

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
#include <map>

#include <fstream>
#include <iomanip>

#include "timing.h"

#include "lineallocator.hpp"
#include "settester.hpp"

#define LLC 3
using namespace std;

void flushLLC() {
//	 asm volatile("wbinvd");
	std::cout << "[FLUSH] Flushing LLC... ";
	int ret = system("sudo cgexec -g cpuset:plumber -g intel_rdt:flushllc sudo /home/fonaro/workspace/flush_llc/flushllc");
	ret += 1;
	std::cout << "[SUCCESS]" << endl;
}

class NeedMoreLinesException : public exception {
	const char* _what;
public:
	NeedMoreLinesException(const char* what) :
		_what(what) {}
	virtual const char* what() const throw () {
		return _what;
	}
};

class CacheSliceDetector {
public:
	using CacheSlices = map<unsigned int, CacheLine<64>::uset>;

	const double anomalyFactor = 1.6;
	const unsigned int initRuns = 500;

private:
	const CacheLine<64>::uset& lines;

	unsigned int slicesCount;
	unsigned int availWays;

	unsigned int linesPerSet;

	unsigned int maxTestLinesCount;
	SetTester<64> tester;

	CacheSlices sliceLines;

	bool verbose;

public:
	CacheSliceDetector(const CacheLine<64>::uset& lines, unsigned int slicesCount,
			unsigned int availWays, unsigned int linesPerSet, bool verbose) :
		lines(lines), slicesCount(slicesCount), availWays(availWays), linesPerSet(linesPerSet),
		maxTestLinesCount(availWays*2 + 1),
		tester(maxTestLinesCount, initRuns, anomalyFactor),
		verbose(verbose) {
	}

	void doubleRuns() {
		tester.doubleRuns();
	}

	CacheLine<64>::ptr findUndetectedLine() {
		for(auto it=lines.begin(); it != lines.end(); ++it) {
			if((*it)->getCacheSlice() < 0) {
				return *it;
			}
		}

		return NULL;
	}

	CacheSlices& getCacheSlices() {
		return sliceLines;
	}

	void flushAllLines() {
		for(auto it=lines.begin(); it != lines.end(); ++it) {
			(*it)->flushFromCache();
		}
	}

	CacheLine<64>::uset findSmallGroupInSlice(CacheLine<64>::vec& fromLines) {
		unsigned int maxIterations = fromLines.size() * fromLines.size();

		for(unsigned int i=0; i < maxIterations; ++i) {
			// Most of the times, the lines won't be in the same set (short access time)
			tester.clear();
			tester.addRandom(fromLines, availWays+2);

			auto sameSet = tester.getSameSetGroup();
			if(sameSet.size() <= availWays) {
				continue;
			}

			return sameSet;
		}

		return CacheLine<64>::uset();
	}

	void findAllLinesOnSameSet(CacheLine<64>::uset& sliceSet) {
		tester.clear();
		tester.add(sliceSet, availWays);

		for(auto line=lines.begin(); line != lines.end(); line++) {
			if(tester.isOnSameSet(*line)) {
				sliceSet.insert(*line);
			}
		}
	}

	CacheLine<64>::uset searchSlice(unsigned int curSlice) {
		CacheLine<64>::uset curSliceSet;

		if(verbose){
			std::cout << "[SLICE: " << setfill(' ') << setw(3) << dec << curSlice << "] Find-Undetected" << std::flush;
		}
		CacheLine<64>::vec undetected = getAllUndetectedLines();

		if(undetected.size() < maxTestLinesCount) {
			if(verbose){
				std::cout << " [FAILED] Not enough undetected lines" << endl;
			}
			throw NeedMoreLinesException("Not enough undetected lines");
		} else {
			if(verbose) {
				std::cout << ", " << std::flush;
			}
		}

		if(curSlice == 11) {
			tester.setVerb(true);
		} else{
			tester.setVerb(false);
		}

		if(verbose) {
			std::cout << "Find-Group" << std::flush;
		}
		curSliceSet = findSmallGroupInSlice(undetected);

		if (curSliceSet.size() <= availWays) {
			curSliceSet.clear();
			if(verbose) {
				std::cout << " [FAILED] Could not detect small group in the same slice" << endl;
			}
			throw NeedMoreLinesException("Could not detect small group in the same slice");
		} else {
			if(verbose) {
				std::cout << ", " << std::flush;
			}
		}

		if(verbose) {
			std::cout << "Find-Entire-Set" << std::flush;
		}
		findAllLinesOnSameSet(curSliceSet);

		double sameSetRatio = (double)curSliceSet.size() / (double)lines.size();
		if(verbose) {
			cout << " [SUCCESS] Total: " << setfill(' ') << setw(6) << curSliceSet.size() << " (ratio: " << sameSetRatio << ")" << endl;
		}

		return curSliceSet;
	}

	void findAllLinesOnExistingSets() {
		if(verbose) {
			std::cout << "[FIND ALL SETS] ... " << std::flush;
		}
		for(auto slice=sliceLines.begin(); slice != sliceLines.end(); ++slice) {
			findAllLinesOnSameSet(slice->second);
			updateSliceId(slice->second, slice->first);
		}
		if(verbose) {
			std::cout << "[SUCCESS]" << endl;
		}
	}

	void updateSliceId(CacheLine<64>::uset linesSet, unsigned int sliceId) {
		auto& curSliceSet = sliceLines[sliceId] = linesSet;

		for(auto i=curSliceSet.begin(); i != curSliceSet.end(); ++i) {
			(*i)->setCacheSlice(sliceId);
		}
	}

	void reset() {
		sliceLines.clear();
		for(auto i=lines.begin(); i != lines.end(); i++) {
			(*i)->resetCacheSlice();
		}
	}

	void groupKnownSlices() {
		for(auto line=lines.begin(); line != lines.end(); line++) {
			auto fileSlice = (*line)->getCacheSliceFromFile();
			if(fileSlice < 0) {
				continue;
			}

			sliceLines[fileSlice].insert(*line);
		}

		for(auto slice=sliceLines.begin(); slice != sliceLines.end(); ++slice) {
			if(slice->second.size() < availWays) {
				sliceLines.erase(slice);
			}
		}
	}

	unsigned int findUndetectedSlice() {
		for(unsigned int i=0; i < slicesCount; ++i) {
			if(sliceLines.find(i) == sliceLines.end()){
				return i;
			}
		}

		return sliceLines.size();
	}

	void detectAllCacheSlices() {
//		flushLLC();

//		if(sliceLines.empty()) {
//			groupKnownSlices();
//		}
		warmup();

		findAllLinesOnExistingSets();

		while(sliceLines.size() < slicesCount) {
			auto curSlice = findUndetectedSlice();
			auto res = searchSlice(curSlice);

			if(res.size() > availWays) {
				updateSliceId(res, curSlice);
			}
		}

		handleUndetectedLines();

		// TODO: Validate all allocations
		for(auto i=sliceLines.begin(); i != sliceLines.end(); ++i) {
			if(i->second.size() < linesPerSet) {
				if(verbose) {
					std::cout << " [FAILED] Not enough lines for each set" << endl;
				}
				throw NeedMoreLinesException("Not enough lines for each set");
			}
		}
	}

	CacheLine<64>::vec getAllUndetectedLines() {
		CacheLine<64>::vec res;
		for(auto l = lines.begin(); l != lines.end(); ++l) {
			if((*l)->getCacheSlice() < 0) {
				res.push_back(*l);
			}
		}

		return res;
	}

	void handleUndetectedLines() {
		if(verbose) {
			std::cout << endl;
		}
		CacheLine<64>::vec undetectedLines = getAllUndetectedLines();

		if (undetectedLines.size() == 0) {
			return;
		}

		if(verbose) {
			std::cout << "[UNDETECTED] Undetected lines: " << undetectedLines.size() << endl;
		}

		std::cout.imbue(std::locale());
		for(auto l = undetectedLines.begin(); l != undetectedLines.end(); ++l) {
			bool addressCorrect = (*l)->getPhysicalAddr() == (*l)->calculatePhyscialAddr();
			if(verbose) {
				std::cout << "[UNDETECTED] 0x" << hex << (*l)->getPhysicalAddr() << " <=> " << (*l)->calculatePhyscialAddr()
					<< " - correct: " << (addressCorrect ? "true" : "false") << endl;
			}

			if(!addressCorrect) {
				throw CacheLineException(this, "Incorrect physical address");
			}

//			flushLLC();
			for(auto i=sliceLines.begin(); i != sliceLines.end(); ++i) {
				tester.clear();
				tester.add(i->second, availWays, availWays);

				if(tester.isOnSameSet(*l)) {
					if(verbose) {
						std::cout << "   - matched slice: " << dec << i->first << endl;
					}
					i->second.insert(*l);
					(*l)->setCacheSlice(i->first);
				}
			}
		}

		undetectedLines = getAllUndetectedLines();
		if(undetectedLines.size() > 0) {
			throw CacheLineException(this, "Found undetected lines");
		}
	}

	void warmup() {
		unsigned int warmupLoops = lines.size() - availWays;

		if(verbose) {
			std::cout << "[WARMUP] ... " << std::flush;
		}

		for(unsigned int i=0; i<warmupLoops; ++i) {
			tester.clear();
			tester.add(lines, i, availWays);
			tester.warmupRun();
		}

		if(verbose){
			std::cout << "[SUCCESS] Avg. median runtime: " << tester.avgMed << endl;
		}
	}
};



#endif /* PLUMBER_SLICEDETECTOR_HPP_ */
