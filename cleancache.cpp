/*
 * cleancache.cpp
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#include <fstream>

#include <sys/types.h>
#include <unistd.h>

#include "benchmarking.h"

#define LLC 3
using namespace std;


int main(int argc, const char* argv[]) {
	std::cout << "Current PID: " << getpid() << endl;

//	CacheInfo l1 = CacheInfo::getCacheLevel(1);
//	CacheInfo l2 = CacheInfo::getCacheLevel(2);
//	CacheInfo l3 = CacheInfo::getCacheLevel(2);
//	l1.print();
//	l2.print();
//	l3.print();

	long set = 0;
	for(int i = 1; i < argc; i++) {
		istringstream ( argv[i] ) >> set;
	}

	if (set >= 0) {
		std::cout << "Requested set: 0x" << hex << set << dec << endl;
	}

	try {
		benchmark(set);
		return 0;
	} catch (exception& e) {
		std::cout << endl << "[EXCEPTION] " << e.what() << endl;
		return 1;
	}
}
