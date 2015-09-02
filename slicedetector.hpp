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

#include "lineallocator.hpp"
#include "settester.hpp"

#define LLC 3
using namespace std;

void flushLLC(bool verbose);

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
	const double anomalyFactor = 1.6;
	const unsigned int initRuns = 64;

	const CacheLine::uset& lines;

	unsigned int slicesCount;
	unsigned int availWays;

	unsigned int linesPerSet;

	unsigned int maxTestLinesCount;
	SetTester tester;

	bool verbose;

public:
	CacheSliceDetector(const CacheLine::uset& lines, unsigned int slicesCount,
			unsigned int availWays, unsigned int linesPerSet, bool verbose) :
		lines(lines), slicesCount(slicesCount), availWays(availWays), linesPerSet(linesPerSet),
		maxTestLinesCount(availWays*2 + 1),
		tester(maxTestLinesCount, initRuns, anomalyFactor),
		verbose(verbose) {
	}

	void doubleRuns() {
		tester.doubleRuns();
	}

	CacheLine::ptr findUndetectedLine() {
		for(auto it=lines.begin(); it != lines.end(); ++it) {
			if((*it)->getCacheSlice() < 0) {
				return *it;
			}
		}

		return NULL;
	}

	void flushAllLines() {
		for(auto it=lines.begin(); it != lines.end(); ++it) {
			(*it)->flushFromCache();
		}
	}

	CacheLine::uset findSmallGroupInSlice(CacheLine::vec& fromLines) {
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

		return CacheLine::uset();
	}

	unsigned int findAllLinesOnSameSet(const CacheLine::uset& sliceSet, unsigned int sliceId) {
		unsigned int count = sliceSet.size();
		for(auto line=sliceSet.begin(); line != sliceSet.end(); line++) {
			(*line)->setCacheSlice(sliceId);
			count += 1;
		}

		tester.clear();
		tester.add(sliceSet, availWays);

		for(auto line=lines.begin(); line != lines.end(); line++) {
			if(tester.isOnSameSet(*line)) {
				if((*line)->getCacheSlice() == (int)sliceId) {
					continue;
				}

				(*line)->setCacheSlice(sliceId);
				count += 1;
			}
		}

		return count;
	}

	unsigned int detectSlice(CacheLine::vec& undetected, unsigned int curSlice) {
		if(verbose){
			std::cout << "[SLICE: " << setfill(' ') << setw(3) << dec << curSlice << "] Find-Undetected" << std::flush;
		}

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

		if(verbose) {
			std::cout << "Find-Group" << std::flush;
		}

		CacheLine::uset curSliceSet = findSmallGroupInSlice(undetected);

		if (curSliceSet.size() <= availWays) {
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

		unsigned int count = findAllLinesOnSameSet(curSliceSet, curSlice);

		double sameSetRatio = (double)curSliceSet.size() / (double)lines.size();
		if(verbose) {
			cout << " [SUCCESS] Total: " << setfill(' ') << setw(6) << curSliceSet.size() << " (ratio: " << sameSetRatio << ")" << endl;
		}

		return count;
	}

	void reset() {
		for(auto i=lines.begin(); i != lines.end(); i++) {
			(*i)->resetCacheSlice();
		}
	}

	void detectAllCacheSlices() {
		reset();
		warmup();

		for(unsigned int curSlice=0; curSlice < slicesCount; ++curSlice) {
			CacheLine::vec undetected = getAllUndetectedLines();
			auto count = detectSlice(undetected, curSlice);
			if(count < linesPerSet) {
				if(verbose) {
					std::cout << " [FAILED] Not enough lines for each set" << endl;
				}
				throw NeedMoreLinesException("Not enough lines for each set");
			}
		}

		CacheLine::vec undetected = getAllUndetectedLines();
		if(undetected.size() > 0) {
			if(verbose) {
				std::cout << "[UNDETECTED] Undetected lines: " << undetected.size() << endl;
			}
			throw CacheLineException(this, "Found undetected lines");
		}

	}

	CacheLine::vec getAllUndetectedLines() {
		CacheLine::vec res;
		for(auto l = lines.begin(); l != lines.end(); ++l) {
			if((*l)->getCacheSlice() < 0) {
				res.push_back(*l);
			}
		}

		return res;
	}

	void warmup() {
		if(verbose) {
			std::cout << "[WARMUP] ... " << std::flush;
		}

		tester.clear();
		for(auto l=lines.begin(); l != lines.end(); l++) {
			tester.add(*l);

			if(tester.testLinesCount == availWays) {
				tester.warmupRun();
				tester.clear();
			}
		}

		if(verbose){
			std::cout << "[SUCCESS] Avg. median runtime: " << tester.avgMed << endl;
		}
	}
};



#endif /* PLUMBER_SLICEDETECTOR_HPP_ */
