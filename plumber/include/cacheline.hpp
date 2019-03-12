/*
 * Author: Liran Funaro <liran.funaro@gmail.com>
 *
 * Copyright (C) 2006-2018 Liran Funaro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
#include "plumber.hpp"

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

class CacheListException : public PlumberException { using PlumberException::PlumberException; };

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
		void insertBack(ptr l);
		void insertBack(const lst& l);
		ptr popFront();
		vector<lst> partition(unsigned int size);
		ptr front() const { return _first; };
		ptr back() const { return _last; };
		unsigned long size() const { return _length; }
		void validate();
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

	char moreData[1];

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

	static void polluteSets(arr partitionsArray, unsigned long partitionsCount,
			volatile bool& continueFlag, bool disableInterupts = false);

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
