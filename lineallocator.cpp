/*
 * lineallocator.cpp
 *
 *  Created on: Sep 2, 2015
 *      Author: liran
 */

#include "lineallocator.hpp"
#include "timing.h"

void CacheLineAllocator::write() {
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


