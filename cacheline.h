/*
 * page.h
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

#ifndef CACHELINE_H_
#define CACHELINE_H_

#include <stdint.h>
#include <stdlib.h>
//#include <sys/mman.h>
#include <malloc.h>
#include <cstdio>
#include <iostream>
#include <exception>

#include <vector>

#include <stddef.h>
#include <sys/mman.h>

using namespace std;

#define PAGE_SIZE (1<<12)
#define PAGE_SHIFT 12
#define PAGEMAP_LENGTH 8
#define POLL_SIZE (1<<5)

#define ARRAY_LENGTH(arr) ( sizeof(arr) / sizeof( (arr)[0] ) )

class CacheLineException: public exception {
	const char* _what;
public:
	CacheLineException(const char* what);
	virtual const char* what() const throw ();
};

template <unsigned int LINE_SIZE>
class CacheLine {
public:
	typedef vector<CacheLine*> vec;

private:
	static CacheLine* poll;
	static CacheLine* pollPos;
	static CacheLine* pollEnd;
	static int pageLinesOffset;

	static unsigned long totalAllocatedPoll;

public:
	struct line_data {
		CacheLine* next;
		unsigned long physcialAddr;

		unsigned long lineRelativePhyscialAddress;
		unsigned long lineSet;
		unsigned int cacheSlice;

		bool markedForDelete;
	};

public:
	union {
		uint8_t buffer[LINE_SIZE];
		struct line_data data;
	} line;

	CacheLine(unsigned int setCount) {
		if (sizeof(*this) != LINE_SIZE) {
			throw CacheLineException("Not line size");
		}

		unsigned long virtualAddress = reinterpret_cast<unsigned long>(this);
		if (virtualAddress % LINE_SIZE != 0) {
			throw CacheLineException("Not aligned to line");
		}

		line.data.next = NULL;
		line.data.physcialAddr = calculate_physcial_addr();

		line.data.lineRelativePhyscialAddress = line.data.physcialAddr / LINE_SIZE;
		line.data.lineSet = calculateSet(setCount);
		line.data.markedForDelete = false;
	}

	unsigned long calculateSet(unsigned int setCount) {
		unsigned long numberOfSlices = 12;
		unsigned long setsPerSlice = setCount/numberOfSlices;

		unsigned long setInSlice = line.data.lineRelativePhyscialAddress % setsPerSlice;

//		line.data.cacheSlice = get_cache_slice();
//		return setInSlice + (line.data.cacheSlice * setsPerSlice);
		return setInSlice;
	}

	unsigned int get_cache_slice() {
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

		static const int h1_bits[] = {16, 17, 21, 23, 24, 27, 33, 34};
		static const int h2_bits[] = {16, 18, 19, 20, 22, 24, 25, 30, 32, 33, 34};
		static const int h3_bits[] = {16, 17, 18, 19, 20, 21, 22, 23, 25, 27, 30, 32};
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

	unsigned int getHashValue(const int *bits, unsigned int bitsCount) {
		const unsigned long phys_addr = line.data.physcialAddr;

		unsigned int hash = 0;
		for (unsigned int i = 0; i < bitsCount; i++) {
			hash ^= (unsigned int)((phys_addr >> bits[i]) & 1);
		}

		return hash;
	}

	void validatePhyscialAddr() const {
		if( line.data.physcialAddr != calculate_physcial_addr()) {
			CacheLineException("Physical address changed!");
		}
	}

	void setNext(CacheLine* next_page) {
		line.data.next = next_page;
	}

	CacheLine* getNext() {
		return line.data.next;
	}

	unsigned long touch() const {
		return line.buffer[0];
	}

	unsigned long touchAll() const {
		CacheLine* curLine = this;
		unsigned long sum = 0;

		while(curLine != NULL) {
			sum += curLine->line.buffer[0];
			curLine->line.data.next;
		}

		return sum;
	}

	void validateAll() const {
		CacheLine* curLine = this;

		while(curLine != NULL) {
			curLine->validatePhyscialAddr();
			curLine->line.data.next;
		}
	}

	unsigned long getSet() const {
		return line.data.lineSet;
	}

	unsigned int getCacheSlice() const {
		return line.data.cacheSlice;
	}

	void lock() {
		mlock(this, sizeof(*this));
	}

	unsigned long getPhysicalAddr() const {
		return line.data.physcialAddr;
	}

	unsigned long calculate_physcial_addr() const {
		// https://shanetully.com/2014/12/translating-virtual-addresses-to-physcial-addresses-in-user-space/

		unsigned long virtAddress = reinterpret_cast<unsigned long>(this);
		unsigned long pageOffset = virtAddress % PAGE_SIZE;

		// Open the pagemap file for the current process
		FILE *pagemap = fopen("/proc/self/pagemap", "rb");

		// Seek to the page that the buffer is on
		unsigned long pageTableOffset = (virtAddress / PAGE_SIZE) * PAGEMAP_LENGTH;
		if (fseek(pagemap, pageTableOffset, SEEK_SET) != 0) {
			throw CacheLineException("Failed to seek pagemap to proper location");
		}

		// The page frame number is in bits 0-54 so read the first 7 bytes and clear the 55th bit
		unsigned long page_frame_number = 0;
		if (fread(&page_frame_number, 1, PAGEMAP_LENGTH - 1, pagemap)
				!= PAGEMAP_LENGTH - 1) {
			throw CacheLineException("Failed to read page frame number");
		}

		fclose(pagemap);

		page_frame_number &= 0x7FFFFFFFFFFFFF;

		return (page_frame_number << PAGE_SHIFT) | pageOffset;
	}

	void print() {
		cout << "VIRT PAGE: 0x" << hex << reinterpret_cast<unsigned long>(this)
					<< " - PHYS PAGE: 0x" << hex << calculate_physcial_addr() << " - SET: 0x"
					<< getSet() << endl;
	}

	static void reallocatePoll() {
		const unsigned long realPollSize = PAGE_SIZE*POLL_SIZE;
		poll = (CacheLine*)mmap(0, realPollSize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		if((reinterpret_cast<unsigned long>(poll) & ((1<<12)-1)) != 0) {
			throw CacheLineException("Not page aligned");
		}

		memset(poll,0,realPollSize);
		pollPos = poll;
		pollEnd = poll + POLL_SIZE;

		totalAllocatedPoll += realPollSize;
	}

	static unsigned long getTotalAllocatedPoll() {
		return totalAllocatedPoll;
	}

	static void setPageOffset(CacheLine* p = NULL) {
		if(p == NULL) {
			pageLinesOffset = -1;
		}

		unsigned long addr = p->getPhysicalAddr();
		pageLinesOffset = (unsigned int)((addr & ((1<<12)-1)) / LINE_SIZE);
		reallocatePoll();
	}

	static void* operator new(size_t size) {
		if(poll == NULL || pollPos >= pollEnd) {
			reallocatePoll();
		}

		void* ret = NULL;

		if(pageLinesOffset < 0) {
			ret = pollPos;
			pollPos += 1;
		} else {
			ret = pollPos + pageLinesOffset;
			pollPos += PAGE_SIZE/LINE_SIZE;
		}

		return ret;
	}

	static void operator delete(void *p) {
//		reinterpret_cast<CacheLine<LINE_SIZE>*>(p)->line.data.markedForDelete = true;
//
//		unsigned long virtAddress = reinterpret_cast<unsigned long>(p);
//		unsigned long virtPageAddress = (virtAddress >> PAGE_SHIFT) << PAGE_SHIFT;
//
//		CacheLine<LINE_SIZE>* cur = reinterpret_cast<CacheLine<LINE_SIZE>*>(virtPageAddress);
//		CacheLine<LINE_SIZE>* end = reinterpret_cast<CacheLine<LINE_SIZE>*>(virtPageAddress + PAGE_SIZE);
//		while(cur < end) {
//			if (!cur->line.data.markedForDelete) {
//				return;
//			}
//			cur++;
//		}
//
//		void* pagePtr = reinterpret_cast<void*>(virtPageAddress);
//		madvise(pagePtr, PAGE_SIZE, MADV_DONTNEED);
	}
};

template <unsigned int LINE_SIZE>
int CacheLine<LINE_SIZE>::pageLinesOffset = -1;

template <unsigned int LINE_SIZE>
CacheLine<LINE_SIZE>* CacheLine<LINE_SIZE>::poll = NULL;

template <unsigned int LINE_SIZE>
CacheLine<LINE_SIZE>* CacheLine<LINE_SIZE>::pollPos = NULL;

template <unsigned int LINE_SIZE>
CacheLine<LINE_SIZE>* CacheLine<LINE_SIZE>::pollEnd = NULL;

template <unsigned int LINE_SIZE>
unsigned long CacheLine<LINE_SIZE>::totalAllocatedPoll = 0;

#endif /* CACHELINE_H_ */
