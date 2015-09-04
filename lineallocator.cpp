/*
 * lineallocator.cpp
 *
 *  Created on: Sep 2, 2015
 *      Author: liran
 */

#include "lineallocator.hpp"
#include "timing.h"

void CacheLineAllocator::allocateAllSets() {
	detector.init(cacheInfo.cache_slices, availableWays, linesPerSet);

	for(unsigned int curSet=0; curSet < setsPerSlice; ++curSet) {
		VERBOSE("[SET: " << setfill(' ') << setw(5) << dec << curSet << "] ");
		detector.restartRuns();
		auto& setLines = getSet(curSet);

		bool moreWork = true;
		bool moreLines = setLines.size() < linesPerSet;
		bool doubleRuns = false;

		unsigned int allocationRetries = 0;
		unsigned int maxRetries = 10;

		while(moreWork) {
			if(moreLines) {
				VERBOSE("[ALLOCATION] Set: " << curSet << " ")
				else if(printAllocationInformation) {std::cout << "Allocating, " << std::flush;}

				allocateSet(curSet, setLines.size() + linesPerSet);
				allocationRetries += 1;
				moreLines = false;
				VERBOSE("[SUCCESS] Total: " << setLines.size() << " lines ("<< ((double)CacheLine::getTotalAllocatedPoll() / (double)(1<<30)) << " GB)" << endl);
			}
			if(doubleRuns) {
				VERBOSE("[DOUBLE RUNS]" << endl)
				else if(printAllocationInformation) {std::cout << "Double-runs, " << std::flush;}
				detector.doubleRuns();
				doubleRuns = false;
			}

			try {
				detector.detectAllCacheSlices(setLines);
				moreWork = false;
			} catch (NeedMoreLinesException& e) {
				VERBOSE("[ERROR] Set: " << dec <<curSet << " - " << e.what() << " => ");
				moreWork = true;
				moreLines = true;

				if(allocationRetries >= maxRetries) {
					bool error = false;
					for(auto l = setLines.begin(); l != setLines.end(); ++l) {
						bool addressCorrect = (*l)->getPhysicalAddr() == (*l)->calculatePhyscialAddr();
						if(!addressCorrect) {
							error = true;
							VERBOSE("   [CHANGED] 0x" << hex << (*l)->getPhysicalAddr() << " != 0x" << (*l)->calculatePhyscialAddr() << endl);
						}
					}

					if (error){
						throw LineAllocatorException("Address changed");
					}

					doubleRuns = true;
					allocationRetries = 0;
				}
			} catch (CacheSliceResetException& e) {
				std::cout.imbue(std::locale());
				VERBOSE("[ERROR] Set: " << dec << curSet << " - " << e.what()
							<< " -- for address: 0x" << hex << ((CacheLine::ptr)e.line())->getPhysicalAddr() << " => ")
				moreWork = true;
				moreLines = false;
				doubleRuns = true;

				discardLine((CacheLine*)e.line());
			} catch (CacheLineException& e) {
				VERBOSE("[ERROR] Set: " << dec << curSet << " - " << e.what() << " => ")
				else if(printAllocationInformation) { std::cout << e.what() << ", " << std::flush; }
				moreWork = true;
				moreLines = false;
				doubleRuns = true;
			}
		}

		VERBOSE("[SUCCESS SET: " << setfill(' ') << setw(5) << dec << curSet << "] " << endl)
		else if(printAllocationInformation) {
			if(curSet % 256 == 255) {
				std::cout << endl << setfill(' ') << setw(5) << dec << "[SET: " << curSet << "] " << endl << std::flush;
			} else if(curSet % 8 == 0) {
				std::cout << "." << std::flush;
			}
		}
	}

	rePartitionSets();
	write();
}

CacheLine::lst CacheLineAllocator::getSet(int set, unsigned int count) {
		auto curSet = getSet(set);
		CacheLine::lst ret;

		for (auto l = curSet.begin(); l != curSet.end() && ret.size() < count; l++) {
			ret.insertBack(*l);
		}

		if (ret.size() < count) {
			throw LineAllocatorException("Not enough lines in set");
		}

		ret.validate();

		return ret;
	}

CacheLine::lst CacheLineAllocator::getAllSets(unsigned int countPerSet) {
	CacheLine::lst ret;

	for(unsigned int set=0; set < getSetsCount(); set++) {
		auto setList = getSet(set, countPerSet);
		ret.insertBack(setList);
	}

	ret.validate();

	return ret;
}

const CacheLine::uset& CacheLineAllocator::allocateSet(unsigned long set, unsigned long count) {
	while(linesSets[set].size() < count) {
		allocateLine();
	}

	CacheLine::GC();

	return getSet(set);
}

void CacheLineAllocator::rePartitionSets() {
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

void CacheLineAllocator::clean(unsigned int maxElementsInGroup) {
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

void CacheLineAllocator::write() {
	char filename[512];
	sprintf(filename, "/home/fonaro/workspace/result/lineallocator-%llu.txt", rdtsc());
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

void CacheLineAllocator::print() const {
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
