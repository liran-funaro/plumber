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

	unsigned int* bestRandomTestGroupSize;
	unsigned int* maxTestGroupRetires;
	SetTester tester;
	bool didWarmup;

	bool verbose;

public:
	CacheSliceDetector(bool verbose) :
		slicesCount(0), availWays(0), linesPerSet(0),
		bestRandomTestGroupSize(NULL), maxTestGroupRetires(NULL), didWarmup(false),
		verbose(verbose) {
	}

	~CacheSliceDetector() {
		discardTestCalculationArrays();
	}

	void discardTestCalculationArrays() {
		if(bestRandomTestGroupSize != NULL) {
			delete[] bestRandomTestGroupSize;
		}
		bestRandomTestGroupSize = NULL;

		if(maxTestGroupRetires != NULL) {
			delete[] maxTestGroupRetires;
		}
		maxTestGroupRetires = NULL;
	}

	void init(unsigned int slicesCount,	unsigned int availWays, unsigned int linesPerSet) {
		this->slicesCount = slicesCount;
		this->availWays = availWays;
		this->linesPerSet = linesPerSet;
		calculateBestRandomTestGroupSize();

		unsigned int maxTestGroupSize = *max_element(bestRandomTestGroupSize, bestRandomTestGroupSize+slicesCount);
		tester.init(maxTestGroupSize + 1);
	}

	void calculateBestRandomTestGroupSize() {
		discardTestCalculationArrays();
		bestRandomTestGroupSize = new unsigned int[slicesCount];
		maxTestGroupRetires = new unsigned int[slicesCount];

		for(unsigned int slice=0; slice < slicesCount; slice++) {
			unsigned int undetectedSlices = slicesCount-slice;
			double minExpectedTestRuns = std::numeric_limits<double>::infinity();
			unsigned int correspondingSize = availWays+1;

			for(unsigned int size=availWays+1; size < minExpectedTestRuns; size++) {
				double curE = calculateExpectedTestsCountForGroupSize(size, undetectedSlices);
				if(curE < minExpectedTestRuns) {
					minExpectedTestRuns = curE;
					correspondingSize = size;
				}
			}

			bestRandomTestGroupSize[slice] = correspondingSize;
			maxTestGroupRetires[slice] = calculateMaximumTestForGroupSize(correspondingSize, undetectedSlices);

			VERBOSE("[CALC Slice: " << slice << "] Best random test group size: " << bestRandomTestGroupSize[slice] << endl);
			VERBOSE("[CALC Slice: " << slice << "] Expected test runs: " << minExpectedTestRuns << endl);
			VERBOSE("[CALC Slice: " << slice << "] Max test runs: " << dec << maxTestGroupRetires[slice] << endl);
		}
	}

	double calculateExpectedTestsCountForGroupSize(unsigned int size, unsigned int slices) {
		double S = size;
		double A = availWays;
		double Z = slices;
		// First we find the expected number of tries until we find a random
		// group of size S where at least A of them are in the same slice as the first line
		// in the group (out of Z slices)
		//
		//             1
		// E1 = -----------------
		//		     |Z-1|^(S-A)
		//		 1 - |---|
		//	    	 | Z |
		//
		double q = (Z-1.) / Z;
		double E1 = 1. / (1. - pow(q, S-A));

		// Then we calculate the expected number of tries until we found A-1 lines in
		// the same slice as the first line out of the S lines.
		// Since we measured a long access time to the first item, then we have at-least
		// A+1 items in the same slice. The first item is always in the slice since we only measure
		// the access time to it.
		// So, we need to find another A-1 lines out of x lines that are in the same set in a group of S-1 items.
		// Where x is between A to S-1 and each x have its own probability.
		//
		//	                          | 1 |^x   |Z-1|^(S-1-x)    A-1
		//	E2 = Sum for x=A to S-1:  |---|   * |---|         * ----- * S
		//	                          | Z |     | Z |            x+1
		//
		double E2 = 0.;

		// If the size is exactly one more then the available ways, then we know that all the group is
		// in the same set, not need to check again
		if(size > availWays+1) {
			double p = 1./Z;
			for(unsigned int x=A; x <= S-1; x++) {
				E2 += pow(p, x) * pow(q, S-1-x) * ( (A-1.)/(x+1.) ) * S;
			}
		}

		return E1 + E2;
	}

	double calculateMaximumTestForGroupSize(unsigned int size, unsigned int slices) {
		double S = size;
		double A = availWays;
		double Z = slices;

		double q = (Z-1.) / Z;
		// The probability to fail at least N times is:
		//
		// 						   |Z-1|^(S-A)*N
		// P(fail*N) = P(fail)^N = |---|
		//           			   | Z |
		//
		// We want N big enough that the probability to fail will be epsilon:
		//
		// P(fail*N) < epsilon  ==> (S-A)*N*log(q) < log(epsilon) ==>
		//
		//	        log(epsilon)
		// ==> N > ---------------
		//		    (S-A) * log(q)

		double logEpsilon = -100;
		return ( logEpsilon / ( (S-A) * log(q) ) ) + 1.;
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

	CacheLine::vec findTestGroupForSlice(CacheLine::vec& fromLines, unsigned int curSlice) {
		for(unsigned int i=0; i < maxTestGroupRetires[curSlice]; ++i) {
			// Most of the times, the lines won't be in the same set (short access time)
			tester.clear();
			tester.addRandom(fromLines, bestRandomTestGroupSize[curSlice]);

			auto testGroup = tester.getSameSetGroup(availWays);

			if(testGroup.size() >= availWays) {
				return testGroup;
			}
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

		if(undetected.size() < bestRandomTestGroupSize[curSlice]) {
			VERBOSE(" [FAILED] Not enough undetected lines" << endl);
			throw NeedMoreLinesException("Not enough undetected lines");
		}

		VERBOSE(", Find-Group");
		CacheLine::vec testGroup = findTestGroupForSlice(undetected, curSlice);

		if (testGroup.size() < availWays) {
			VERBOSE(" [FAILED] Could not detect small group in the same slice" << endl);
			throw NeedMoreLinesException("Could not detect small group in the same slice");
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
			std::cout << "[SUCCESS] "
					  << "Hit access time: " << tester.avgHitAccessTime << " - "
					  << "Miss access time: " << tester.avgMissAccessTime << endl;
		}

		didWarmup = true;
	}
};



#endif /* PLUMBER_SLICEDETECTOR_HPP_ */
