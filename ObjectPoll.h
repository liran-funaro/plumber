/*
 * ObjectPoll.h
 *
 *  Created on: Sep 1, 2015
 *      Author: liran
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
#define PAGE_FRAME_MASK(ptr) ADDR_TO_PTR( (PTR_TO_ADDR(ptr) & (~0L<<12)) )

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
