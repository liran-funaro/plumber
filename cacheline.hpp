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

#include "ObjectPoll.h"

using namespace std;

class CacheLineException: public exception {
protected:
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
	string whatStr;
public:
	CacheSliceResetException(void* line, unsigned long was, unsigned long changed) : CacheLineException(line, "Cache slice must be only set once"),
	was(was), changed(changed){
		stringstream ss;
		ss.imbue(std::locale());
		ss << "Cache slice changed from " << dec << was << " to: " << changed;
		whatStr = ss.str();
		_what = whatStr.c_str();
	}
};

class CacheLine {
public:
	using ptr = CacheLine*;
	using arr = ptr*;
	using vec = vector<ptr>;
	using uset = set<ptr>;

	class lst {
	private:
		ptr _first;
		ptr _last;
		unsigned long _length;

	public:
		lst() : _first(NULL), _last(NULL), _length(0) {}

		void insertBack(ptr l) {
			if(_last != NULL) {
				_last->setNext(l);
			} else {
				_first = l;
			}

			_last = l;
			_length += 1;
			_last->setNext(NULL);
		}

		void insertBack(const lst& l) {
			if(_last != NULL) {
				_last->setNext(l.front());
			} else {
				_first = l.front();
			}

			_last = l.back();

			// We already added the first line
			_length += l.size();
			_last->setNext(NULL);
		}

		ptr front() const { return _first; };
		ptr back() const { return _last; };
		unsigned long size() const { return _length; }
	};

private:
	static ObjectPoll* poll;
	static map<unsigned long, unsigned int> oldAddressMap;

	static int pollute_dummy;

public:

public:
	ptr next;
	unsigned int lineSize;
	unsigned long physcialAddr;
	unsigned long lineRelativePhyscialAddress;
	unsigned long lineSet;
	int cacheSlice;
	unsigned int inSliceSetCount;

	char moreData[0];

	CacheLine(unsigned int lineSize, unsigned int _inSliceSetCount);
	void calculateSet();
	void validatePhyscialAddr() const;

	inline void setNext(ptr next_page) {
		next = next_page;
	}

	inline ptr getNext() const {
		return next;
	}

	void validateAll() const;

	inline unsigned long getSet() const {
		return lineSet;
	}

	inline unsigned long getInSliceSet() const {
		return lineRelativePhyscialAddress % inSliceSetCount;
	}

	inline int getCacheSlice() const {
		return cacheSlice;
	}

	void setCacheSlice(unsigned long _cacheSlice);

	void resetCacheSlice();

	inline unsigned long getPhysicalAddr() const {
		return physcialAddr;
	}

	unsigned long calculatePhyscialAddr() const;

	/*********************************************************************************************
	 * Pollute Control
	 *********************************************************************************************/
	inline void flushFromCache() {
		asm volatile ("clflush (%0)" :: "r"(this));
	}

	void flushSets();

	void polluteSets(unsigned int setSize, unsigned long runs, volatile bool& continueFlag,
			unsigned long eachSetRuns = 1, bool disableInterupts = false);

	/*********************************************************************************************
	 * Poll Control
	 *********************************************************************************************/
	static void allocatePoll(unsigned int setLineSize);
	static void GC();
	static unsigned long getTotalAllocatedPoll();
	static void setPageOffset(ptr p = NULL);
	static void operator delete(void *p) ;
	static void* operator new(size_t size);

	/*********************************************************************************************
	 * Backup
	 *********************************************************************************************/
	void print();

	unsigned int get_cache_slice();

	unsigned int getHashValue(const int *bits, unsigned int bitsCount) ;

	static unsigned long stringToAddr(string& str);

	int getCacheSliceFromFile() ;
};

#endif /* PLUMBER_CACHELINE_H_ */
