/*
 * page.h
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

#include "cacheline.hpp"

ObjectPoll* CacheLine::poll;
map<unsigned long, unsigned int> CacheLine::oldAddressMap;
int CacheLine::pollute_dummy;

CacheLine::CacheLine(unsigned int lineSize, unsigned int _inSliceSetCount) {
	if (sizeof(*this) > lineSize) {
		throw CacheLineException(this, "Object is bigger then line size");
	}

	unsigned long virtualAddress = PTR_TO_ADDR(this);
	if (virtualAddress % lineSize != 0) {
		throw CacheLineException(this, "Not aligned to line size");
	}

	mlock(this, sizeof(*this));

	unsigned int roundedSetCount;
	for (roundedSetCount=1; roundedSetCount<_inSliceSetCount; roundedSetCount*=2 );
	if(roundedSetCount != _inSliceSetCount) {
		throw CacheLineException(this, "In slice set count must be a power of 2");
	}

	next = NULL;
	physcialAddr = calculatePhyscialAddr();

	lineRelativePhyscialAddress = physcialAddr / lineSize;

	inSliceSetCount = _inSliceSetCount;
	cacheSlice = -1;

	calculateSet();
}

void CacheLine::calculateSet() {
	lineSet = getInSliceSet()
		| (cacheSlice < 0 ? 0 : (cacheSlice * inSliceSetCount));
}

void CacheLine::validatePhyscialAddr() const {
	if(physcialAddr != calculatePhyscialAddr()) {
		CacheLineException(this, "Physical address changed!");
	}
}

void CacheLine::validateAll() const {
	const CacheLine* curLine = this;

	while(curLine != NULL) {
		curLine->validatePhyscialAddr();
		curLine = curLine->getNext();
	}
}

void CacheLine::setCacheSlice(unsigned long _cacheSlice) {
	if(cacheSlice < 0 || cacheSlice == cacheSlice) {
		cacheSlice = _cacheSlice;
	} else {
		throw CacheSliceResetException(this, cacheSlice, cacheSlice);
	}

	calculateSet();
}

void CacheLine::resetCacheSlice() {
	cacheSlice = -1;
	calculateSet();
}


unsigned long CacheLine::calculatePhyscialAddr() const {
	return ObjectPoll::calculatePhyscialAddr((void*)this);
}

/*********************************************************************************************
 * Pollute Control
 *********************************************************************************************/
void CacheLine::flushSets() {
	ptr curline = this;
	while (curline != NULL) {
		curline->flushFromCache();
		curline = ((volatile ptr) curline)->next;
	}
}

void CacheLine::polluteSets(unsigned int setSize, unsigned long runs, volatile bool& continueFlag,
		unsigned long eachSetRuns, bool useMemFence, bool disableInterupts) {
	continueFlag = true;

	if(disableInterupts) {
		iopl(3);
	}

	for (unsigned long run = 0; continueFlag && (runs == 0 || run < runs); ++run) {
		if(disableInterupts) {
			__asm__ __volatile__("cli");
		}

		ptr curline = this;

		unsigned long setLinesCount = 0;
		unsigned long setLinesRuns = eachSetRuns;
		ptr setline = this;

		while (curline != NULL) {
			curline = ((volatile ptr) curline)->next;

			setLinesCount = (setLinesCount + 1) % setSize;
			if(setLinesCount == 0) {
				if(useMemFence) {
					asm volatile("mfence");
				}

				setLinesRuns -= 1;
				if(setLinesRuns == 0) {
					setline = curline;
					setLinesRuns = eachSetRuns;
				} else {
					curline = setline;
				}
			}
		}

		if(disableInterupts) {
			__asm__ __volatile__("sti");
		}

		if(useMemFence) {
			asm volatile("mfence");
		}
	}
}

/*********************************************************************************************
 * Poll Control
 *********************************************************************************************/
void CacheLine::allocatePoll(unsigned int setLineSize) {
	if(poll == NULL) {
		poll = new ObjectPoll(setLineSize, POLL_SIZE);
	}
}

void CacheLine::GC() {
	if(poll != NULL) {
		poll->GC();
	}
}

unsigned long CacheLine::getTotalAllocatedPoll() {
	if(poll != NULL) {
		return poll->getTotalAllocatedPoll();
	}

	return 0;
}

void CacheLine::setPageOffset(ptr p) {
	if(poll != NULL) {
		poll->setPageOffset(p);
	}
}

void CacheLine::operator delete(void *p) {
	if(poll != NULL) {
		poll->deleteObject(p);
	}
}

void* CacheLine::operator new(size_t size) {
	if(poll == NULL) {
		return NULL;
	}

	return poll->newObject();
}

/*********************************************************************************************
 * Backup
 *********************************************************************************************/
void CacheLine::print() {
	cout << "VIRT PAGE: 0x" << hex << reinterpret_cast<unsigned long>(this)
				<< " - PHYS PAGE: 0x" << hex << calculatePhyscialAddr() << " - SET: 0x"
				<< getSet() << endl;
}

unsigned int CacheLine::get_cache_slice() {
	// On a 4-core machine, the CPU's hash function produces a 2-bit
	// cache slice number, where the two bits are defined by "h1" and
	// "h2":
	//
	// h1 function:
	//   static const int bits[] = { 18, 19, 21, 23, 25, 27, 29, 30, 31 };
	// h2 function:
	//   static const int bits[] = { 17, 19, 20, 21, 22, 23, 24, 26, 28, 29, 31 };
	//
	// This hash function is described in the paper "Practical Timing
	// Side Channel Attacks Against Kernel Space ASLR".
	//
	// On a 2-core machine, the CPU's hash function produces a 1-bit
	// cache slice number which appears to be the XOR of h1 and h2.

	// XOR of h1 and h2:

	static const int h1_bits[] = { 16, 17, 21, 23, 24, 27, 33, 34 };
	static const int h2_bits[] = { 16, 18, 19, 20, 22, 24, 25, 30, 32, 33,
			34 };
	static const int h3_bits[] = { 16, 17, 18, 19, 20, 21, 22, 23, 25, 27,
			30, 32 };
	//		static const int h4_bits[] = {17, 18, 19, 20, 21, 22, 23, 25, 27, 30, 32, 36};
	//		static const int h5_bits[] = {18, 19, 20, 22, 24, 25, 30, 32, 33, 34, 35, 36};

	unsigned int hash = 0;
	hash |= (getHashValue(h1_bits, ARRAY_LENGTH(h1_bits)) << 0);
	hash |= (getHashValue(h2_bits, ARRAY_LENGTH(h2_bits)) << 1);
	hash |= (getHashValue(h3_bits, ARRAY_LENGTH(h3_bits)) << 2);
	//		hash |= (getHashValue(h4_bits, ARRAY_LENGTH(h4_bits)) << 3);
	//		hash |= (getHashValue(h5_bits, ARRAY_LENGTH(h5_bits)) << 4);

	return hash;
}

unsigned int CacheLine::getHashValue(const int *bits, unsigned int bitsCount) {
	unsigned int hash = 0;
	for (unsigned int i = 0; i < bitsCount; i++) {
		hash ^= (unsigned int) ((physcialAddr >> bits[i]) & 1);
	}

	return hash;
}

unsigned long CacheLine::stringToAddr(string& str) {
	unsigned long addr;
	std::stringstream hexss;
	hexss << std::hex << str;
	hexss >> addr;
	return addr;
}

int CacheLine::getCacheSliceFromFile() {
	if (oldAddressMap.size() == 0) {
		ifstream datafile;
		datafile.open("cacheline.txt");

		while (datafile) {
			string s;
			if (!getline(datafile, s))
				break;
			if (s[0] == '#') {
				continue;
			}

			istringstream ss(s);
			vector<string> record;

			while (ss) {
				string s;
				if (!getline(ss, s, ';'))
					break;
				record.push_back(s);
			}

			unsigned long curSlice = stringToAddr(record[1]);
			unsigned long curAddr = stringToAddr(record[2]);
			oldAddressMap[curSlice] = curAddr;
		}
	}

	auto it = oldAddressMap.find(getPhysicalAddr());
	if (it != oldAddressMap.end()) {
		std::cout << endl << "FOUND ADDR" << endl;
		return it->second;
	}

	return -1;
}
