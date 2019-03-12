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
#ifndef OBJECTPOLL_H_
#define OBJECTPOLL_H_

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
#define PAGE_FRAME_MASK(ptr) ADDR_TO_PTR( (PTR_TO_ADDR(ptr) & (~0UL<<12)) )

#define ARRAY_LENGTH(arr) ( sizeof(arr) / sizeof( (arr)[0] ) )

class ObjectPollException: public exception {
protected:
	string _what;
public:
	ObjectPollException(const char* what):
		_what(what)  {}
	virtual const char* what() const throw () {
		return _what.c_str();
	}
};

class ObjectPoll {
	unsigned long objectSize;
	unsigned long pollSize;

	char* poll;
	char* pollGCPos;
	char* pollPos;
	char* pollEnd;

	bool usePageOffset;
	unsigned long pageOffset;

	unsigned long freedPages;

public:
	ObjectPoll(unsigned long objectSize, unsigned long pollSize);
	virtual ~ObjectPoll();

	void GC();
	unsigned long getTotalAllocatedPoll();
	void setPageOffset(void* p = NULL);

	void* newObject();
	void deleteObject(void *p);

	static unsigned long calculatePhyscialAddr(void* ptr);

private:
	void freeArea(void* p, unsigned long size);
};

#endif /* OBJECTPOLL_H_ */
