/*
 * page.cpp
 *
 *  Created on: Apr 1, 2015
 *      Author: liran
 */

#include <stddef.h>
#include <sys/mman.h>

#include "cacheline.h"

using namespace std;

CacheLineException::CacheLineException(const char* what) :
		_what(what) {
}

const char* CacheLineException::what() const throw () {
	return _what;
}

