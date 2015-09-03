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
#include <cmath>
#include <limits>

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

#define VERBOSE(streamline) \
	if(verbose) { \
		std::cout << streamline << std::flush; \
	}

class CacheSliceDetector {
	const double anomalyFactor = 1.6;
	const unsigned int initRuns = 64;

	unsigned int slicesCount;
	unsigned int availWays;

	unsigned int linesPerSet;

	unsigned int bestRandomTestGroupSize;
	unsigned int maxTestGroupRetires;
	SetTester tester;
	bool didWarmup;

	bool verbose;

public:
	CacheSliceDetector(bool verbose) :
		slicesCount(0), availWays(0), linesPerSet(0),
		bestRandomTestGroupSize(0), maxTestGroupRetires(0), didWarmup(false),
		verbose(verbose) {
	}

	void init(unsigned int slicesCount,	unsigned int availWays, unsigned int linesPerSet) {
		this->slicesCount = slicesCount;
		this->availWays = availWays;
		this->linesPerSet = linesPerSet;
		calculateBestRandomTestGroupSize();

		tester.init(bestRandomTestGroupSize + 1);
	}

	void calculateBestRandomTestGroupSize() {
		double minExpectedTestRuns = std::numeric_limits<double>::infinity();
		unsigned int correspondingSize = 3;

		for(unsigned int size=availWays+1; size < minExpectedTestRuns; size++) {
			double curE = calculateExpectedTestsCountForGroupSize(size);
			if(curE < minExpectedTestRuns) {
				minExpectedTestRuns = curE;
				correspondingSize = size;
			}
		}

		bestRandomTestGroupSize = correspondingSize;
		maxTestGroupRetires = pow(minExpectedTestRuns, 4.);

		VERBOSE("[CALC] Best random test group size: " << bestRandomTestGroupSize << endl);
		VERBOSE("[CALC] Expected test runs: " << minExpectedTestRuns << endl);
	}

	double calculateExpectedTestsCountForGroupSize(unsigned int size) {
		//
		//                       1                               availWays-1
		// E = ---------------------------------------------- + ------------- * (size-1)
		//          (  slicesCount-1  ) ^ (size - availWays)      availWays
		//      1 - ( --------------- )
		//          (   slicesCount   )

		double q = double(slicesCount-1)/double(slicesCount);
		double left = 1./(1. - pow(q, double(size-availWays)));
		double right = (double(availWays-1)/double(availWays)) * double(size-1);

		return left + right;
	}

	void doubleRuns() {
		tester.doubleRuns();
	}

	void restartRuns() {
		tester.restartRuns();
	}

	static void flushAllLines(const CacheLine::uset& lines) {
		for(auto it=lines.begin(); it != lines.end(); ++it) {
			(*it)->flushFromCache();
		}
	}

	CacheLine::vec findTestGroupForSlice(CacheLine::vec& fromLines) {
		for(unsigned int i=0; i < maxTestGroupRetires; ++i) {
			// Most of the times, the lines won't be in the same set (short access time)
			tester.clear();
			tester.addRandom(fromLines, bestRandomTestGroupSize);

			auto testGroup = tester.getSameSetGroup(availWays);
			if(testGroup.size() < availWays) {
				continue;
			}

			return testGroup;
		}

		return CacheLine::vec();
	}

	unsigned int findAllLinesOnSameSet(const CacheLine::uset& lines, const CacheLine::vec& testGroup, unsigned int sliceId) {
		unsigned int count = 0;

		tester.clear();
		tester.add(testGroup, availWays);

		for(auto line=lines.begin(); line != lines.end(); line++) {
			if(tester.isOnSameSet(*line)) {
				(*line)->setCacheSlice(sliceId);
				count += 1;
			}
		}

		for(auto line=testGroup.begin(); line != testGroup.end(); line++) {
			if((*line)->getCacheSlice() != (int)sliceId) {
				(*line)->setCacheSlice(sliceId);
				count += 1;
			}
		}

		return count;
	}

	unsigned int detectSlice(const CacheLine::uset& lines, unsigned int curSlice) {
		VERBOSE("[SLICE: " << setfill(' ') << setw(3) << dec << curSlice << "] Find-Undetected");
		CacheLine::vec undetected = getAllUndetectedLines(lines);

		if(undetected.size() < bestRandomTestGroupSize) {
			VERBOSE(" [FAILED] Not enough undetected lines" << endl);
			throw NeedMoreLinesException("Not enough undetected lines");
		}

		VERBOSE(", Find-Group");
		CacheLine::vec testGroup = findTestGroupForSlice(undetected);

		if (testGroup.size() < availWays) {
			VERBOSE(" [FAILED] Could not detect small group in the same slice" << endl);
			throw NeedMoreLinesException("Could not detect small group in the same slice");
		} else {
			if(verbose) {
				std::cout << ", " << std::flush;
			}
		}

		VERBOSE(", Find-Entire-Set");
		unsigned int count = findAllLinesOnSameSet(lines, testGroup, curSlice);

		double sameSetRatio = (double)count / (double)lines.size();
		VERBOSE(" [SUCCESS] Total: " << setfill(' ') << setw(6) << count << " (ratio: " << sameSetRatio << ")" << endl);

		if(count < linesPerSet) {
			VERBOSE(" [FAILED] Not enough lines for each set" << endl);
			throw NeedMoreLinesException("Not enough lines for each set");
		}

		return count;
	}

	void reset(const CacheLine::uset& lines) {
		for(auto i=lines.begin(); i != lines.end(); i++) {
			(*i)->resetCacheSlice();
		}
	}

	void detectAllCacheSlices(const CacheLine::uset& lines) {
		if(!didWarmup) {
			warmup(lines);
		}
		reset(lines);

		for(unsigned int curSlice=0; curSlice < slicesCount; ++curSlice) {
			detectSlice(lines, curSlice);
		}

		CacheLine::vec undetected = getAllUndetectedLines(lines);
		if(undetected.size() > 0) {
			if(verbose) {
				std::cout << "[UNDETECTED] Undetected lines: " << undetected.size() << endl;
			}
			throw CacheLineException(this, "Found undetected lines");
		}

	}

	static CacheLine::vec getAllUndetectedLines(const CacheLine::uset& lines) {
		CacheLine::vec res;
		for(auto l = lines.begin(); l != lines.end(); ++l) {
			if((*l)->getCacheSlice() < 0) {
				res.push_back(*l);
			}
		}

		return res;
	}

	template<typename T>
	void warmup(const T& lines) {
		if(verbose) {
			std::cout << "[WARMUP] ... " << std::flush;
		}

		for(auto l=lines.begin(); l != lines.end(); l++) {
			tester.clear();
			tester.add(*l);
			tester.warmupRun();
		}

		if(verbose){
			std::cout << "[SUCCESS] Avg. median runtime: " << tester.avgMed << endl;
		}

		didWarmup = true;
	}
};



#endif /* PLUMBER_SLICEDETECTOR_HPP_ */
