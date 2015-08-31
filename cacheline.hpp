/*
 * page.h
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

#ifndef PLUMBER_CACHELINE_H_
#define PLUMBER_CACHELINE_H_

#include <stddef.h>
#include <sys/mman.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <locale>
#include <set>
#include <vector>
#include <map>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <sys/io.h>

using namespace std;

#define PAGE_SIZE (1L<<12)
#define PAGE_SHIFT 12
#define PAGEMAP_LENGTH 8
#define POLL_SIZE (1L<<34)

#define PTR_TO_ADDR(ptr) ( reinterpret_cast<unsigned long>(ptr) )
#define ADDR_TO_PTR(addr) ( reinterpret_cast<void*>(addr) )

#define PAGE_MASK(ptr) ( PTR_TO_ADDR(ptr) & ( (1<<12) - 1) )
#define PAGE_FRAME_MASK(ptr) ADDR_TO_PTR( (PTR_TO_ADDR(ptr) & (~0L<<12)) )

#define ARRAY_LENGTH(arr) ( sizeof(arr) / sizeof( (arr)[0] ) )

class CacheLineException: public exception {
	const void* _line;
	const char* _what;
public:
	CacheLineException(const void* line, const char* what):
		_line(line), _what(what)  {}
	virtual const char* what() const throw () {
		return _what;
	}

	virtual const void* line() const {
		return _line;
	}
};

class CacheSliceResetException: public CacheLineException {
public:
	const unsigned long was;
	const unsigned long changed;
public:
	CacheSliceResetException(void* line, unsigned long was, unsigned long changed) : CacheLineException(line, "Cache slice must be only set once"),
	was(was), changed(changed){}
};

template <unsigned int LINE_SIZE>
class CacheLine {
public:
	using ptr = CacheLine*;
	using arr = ptr*;
	using vec = vector<ptr>;
	using uset = set<ptr>;

private:
	static CacheLine* poll;
	static CacheLine* pollGCPos;
	static CacheLine* pollPos;
	static CacheLine* pollEnd;
	static int pageOffset;
	static map<unsigned long, unsigned int> oldAddressMap;

	static unsigned long freedPages;

	static int pollute_dummy;

public:
	struct line_data {
		CacheLine* next;
		unsigned long physcialAddr;

		unsigned long lineRelativePhyscialAddress;
		unsigned long lineSet;

		int cacheSlice;

		unsigned int setCount;
	};

public:
	union {
		uint8_t buffer[LINE_SIZE];
		struct line_data data;
	} line;

	CacheLine(unsigned int setCount) {
		if (sizeof(*this) != LINE_SIZE) {
			throw CacheLineException(this, "Not line size");
		}

		mlock(this, sizeof(*this));

		unsigned int roundedSetCount;
		for (roundedSetCount=1; roundedSetCount<setCount; roundedSetCount*=2 );
		if(roundedSetCount != setCount) {
			throw CacheLineException(this, "Set count must be a power of 2");
		}

		unsigned long virtualAddress = reinterpret_cast<unsigned long>(this);
		if (virtualAddress % LINE_SIZE != 0) {
			throw CacheLineException(this, "Not aligned to line");
		}

		line.data.next = NULL;
		line.data.physcialAddr = calculatePhyscialAddr();

		line.data.lineRelativePhyscialAddress = line.data.physcialAddr / LINE_SIZE;

		line.data.setCount = setCount;
		line.data.cacheSlice = -1;

		calculateSet();
	}

	void calculateSet() {
		line.data.lineSet = getInSliceSet()
			| (line.data.cacheSlice < 0 ? 0 : (line.data.cacheSlice * line.data.setCount));
	}

	void validatePhyscialAddr() const {
		if( line.data.physcialAddr != calculatePhyscialAddr()) {
			CacheLineException("Physical address changed!");
		}
	}

	void setNext(CacheLine* next_page) {
		line.data.next = next_page;
	}

	CacheLine* getNext() {
		return line.data.next;
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

	unsigned long getInSliceSet() const {
		return line.data.lineRelativePhyscialAddress % line.data.setCount;
	}

	int getCacheSlice() const {
		return line.data.cacheSlice;
	}

	void setCacheSlice(unsigned long cacheSlice) {
		if(line.data.cacheSlice < 0 || line.data.cacheSlice == cacheSlice) {
			line.data.cacheSlice = cacheSlice;
		} else {
			std::cout.imbue(std::locale());
			std::cout << "Cache slice was: " << line.data.cacheSlice
					  << " -- and changed to: " << cacheSlice
					  << " -- for address: 0x" << hex << getPhysicalAddr()
					  << endl;
			throw CacheSliceResetException(this, line.data.cacheSlice, cacheSlice);
		}

		calculateSet();
	}

	void resetCacheSlice() {
		line.data.cacheSlice = -1;
		calculateSet();
	}

	unsigned long getPhysicalAddr() const {
		return line.data.physcialAddr;
	}

	unsigned long calculatePhyscialAddr() const {
		// https://shanetully.com/2014/12/translating-virtual-addresses-to-physcial-addresses-in-user-space/

		unsigned long virtAddress = reinterpret_cast<unsigned long>(this);
		unsigned long pageOffset = virtAddress % PAGE_SIZE;

		// Open the pagemap file for the current process
		FILE *pagemap = fopen("/proc/self/pagemap", "rb");

		// Seek to the page that the buffer is on
		unsigned long pageTableOffset = (virtAddress / PAGE_SIZE) * PAGEMAP_LENGTH;
		if (fseek(pagemap, pageTableOffset, SEEK_SET) != 0) {
			throw CacheLineException(this, "Failed to seek pagemap to proper location");
		}

		// The page frame number is in bits 0-54 so read the first 7 bytes and clear the 55th bit
		unsigned long page_frame_number = 0;
		if (fread(&page_frame_number, 1, PAGEMAP_LENGTH - 1, pagemap)
				!= PAGEMAP_LENGTH - 1) {
			throw CacheLineException(this, "Failed to read page frame number");
		}

		fclose(pagemap);

		page_frame_number &= 0x7FFFFFFFFFFFFF;

		return (page_frame_number << PAGE_SHIFT) | pageOffset;
	}

	/*********************************************************************************************
	 * Pollute Control
	 *********************************************************************************************/
	void flushFromCache() {
		asm volatile ("clflush (%0)" :: "r"(this));
	}

	static void polluteSets(CacheLine::arr lines, unsigned long count, unsigned int setSize, unsigned long runs) {
		for (unsigned long run = 0; run < runs; run++) {
			for (unsigned int i = 0; i < count; i++) {
				pollute_dummy += *(volatile int *) lines[i];

				if(i % setSize == 0) {
					asm volatile("mfence");
				}
			}
		}

		asm volatile("mfence");
	}

	static void polluteSets(CacheLine::arr lines, unsigned long count, unsigned int setSize, volatile bool& continueFlag) {
		continueFlag = true;

		while (continueFlag) {
			for (unsigned int i = 0; i < count; i++) {
				pollute_dummy += *(volatile int *) lines[i];

				if(i % setSize == 0) {
					asm volatile("mfence");
				}
			}
		}

		asm volatile("mfence");
	}

	void polluteSets(unsigned int setSize, unsigned long runs) {
		iopl(3);

		for (unsigned long run = 0; run < runs; run++) {
			__asm__ __volatile__("cli");

			unsigned int i = 0;
			CacheLine::ptr curline = this;
			while (curline != NULL) {
				curline = ((volatile CacheLine::ptr) curline)->line.data.next;

				if(++i % setSize == 0) {
					asm volatile("mfence");
				}
			}

			__asm__ __volatile__("sti");
			asm volatile("mfence");
		}
	}

	void polluteSets(unsigned int setSize, volatile bool& continueFlag) {
		iopl(3);

		while (continueFlag) {
			__asm__ __volatile__("cli");

			unsigned int i = 0;
			CacheLine::ptr curline = this;
			while (curline != NULL) {
				curline = ((volatile CacheLine::ptr) curline)->line.data.next;

				if(++i % setSize == 0) {
					asm volatile("mfence");
				}
			}

			__asm__ __volatile__("sti");
			asm volatile("mfence");
		}

		asm volatile("mfence");
	}

	/*********************************************************************************************
	 * Poll Control
	 *********************************************************************************************/
	static void allocatePoll() {
		if(poll != NULL) {
			return;
		}

		poll = (CacheLine*)mmap(0, POLL_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		if(PAGE_MASK(poll) != 0) {
			throw CacheLineException(poll, "Not page aligned");
		}

		pollGCPos = poll;
		pollPos = poll;
		pollEnd = poll + POLL_SIZE/sizeof(*poll);
	}

	static void GC() {
		for(char* pollIter = (char*)pollGCPos; pollIter < (char*)pollPos; pollIter += PAGE_SIZE) {
			auto buff = reinterpret_cast<unsigned long*>(pollIter);
			bool isCleared = true;

			for (unsigned int i=0; i < PAGE_SIZE/sizeof(*buff); ++i) {
				if (buff[i] != 0) {
					isCleared = false;
					break;
				}
			}

			if (isCleared) {
				madvise(pollIter, PAGE_SIZE, MADV_DONTNEED);
				freedPages += 1;
			}
		}

		pollGCPos = reinterpret_cast<ptr>( PAGE_FRAME_MASK(pollPos) );
	}

	static unsigned long getTotalAllocatedPoll() {
		return PTR_TO_ADDR(pollPos) - PTR_TO_ADDR(poll) - freedPages*PAGE_SIZE;
	}

	static void setPageOffset(CacheLine* p = NULL) {
		if(p == NULL) {
			pageOffset = -1;
		}

		pageOffset = PAGE_MASK(p);
	}

	static void operator delete(void *p) {
		memset((ptr)p, 0, LINE_SIZE);
	}

	static void* operator new(size_t size) {
		if(poll == NULL) {
			allocatePoll();
		}

		if(pollPos >= pollEnd) {
			throw CacheLineException(NULL, "Out of Poll");
		}

//		if(PAGE_MASK(pollPos) == 0) {
//			if(PTR_TO_ADDR(pollPos) >= 0x00800000000L) {
//				memset((ptr)pollPos, 0, PAGE_SIZE);
//				pollPos += PAGE_SIZE/LINE_SIZE;
//			}
//		}

		if(pageOffset >= 0) {
			unsigned int curOffset = PAGE_MASK(pollPos);
			while(curOffset != pageOffset) {
				operator delete(pollPos);
				pollPos += 1;
				curOffset = PAGE_MASK(pollPos);
			}
		}

		return pollPos++;
	}

	/*********************************************************************************************
	 * Backup
	 *********************************************************************************************/
	void print() {
		cout << "VIRT PAGE: 0x" << hex << reinterpret_cast<unsigned long>(this)
					<< " - PHYS PAGE: 0x" << hex << calculatePhyscialAddr() << " - SET: 0x"
					<< getSet() << endl;
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

	unsigned int getHashValue(const int *bits, unsigned int bitsCount) {
		const unsigned long phys_addr = line.data.physcialAddr;

		unsigned int hash = 0;
		for (unsigned int i = 0; i < bitsCount; i++) {
			hash ^= (unsigned int) ((phys_addr >> bits[i]) & 1);
		}

		return hash;
	}

	static unsigned long stringToAddr(string& str) {
		unsigned long addr;
		std::stringstream hexss;
		hexss << std::hex << str;
		hexss >> addr;
		return addr;
	}

	int getCacheSliceFromFile() {
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
};

template <unsigned int LINE_SIZE>
int CacheLine<LINE_SIZE>::pageOffset = -1;

template <unsigned int LINE_SIZE>
CacheLine<LINE_SIZE>* CacheLine<LINE_SIZE>::poll = NULL;

template <unsigned int LINE_SIZE>
CacheLine<LINE_SIZE>* CacheLine<LINE_SIZE>::pollGCPos = NULL;

template <unsigned int LINE_SIZE>
CacheLine<LINE_SIZE>* CacheLine<LINE_SIZE>::pollPos = NULL;

template <unsigned int LINE_SIZE>
CacheLine<LINE_SIZE>* CacheLine<LINE_SIZE>::pollEnd = NULL;

template <unsigned int LINE_SIZE>
unsigned long CacheLine<LINE_SIZE>::freedPages = 0;

template <unsigned int LINE_SIZE>
map<unsigned long, unsigned int> CacheLine<LINE_SIZE>::oldAddressMap;

template <unsigned int LINE_SIZE>
int CacheLine<LINE_SIZE>::pollute_dummy;

#endif /* PLUMBER_CACHELINE_H_ */
