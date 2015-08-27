/*
 * pageallocator.cpp
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

#include <iostream>
#include <stdlib.h>
#include "pageallocator.h"
using namespace std;

WaysException::WaysException(const char* what) :
		_what(what) {
}

const char* WaysException::what() const throw () {
	return _what;
}

//unsigned long CacheLineAllocator::touchWay(unsigned int way) {
//	if (way >= waysCount) {
//		throw WaysException("Ways out of bound");
//	}
//
//	return waysLines[way]->touchAll();
//}

