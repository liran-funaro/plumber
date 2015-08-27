/*
 * cpuid_cache.h
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

#ifndef CPUID_CACHE_H_
#define CPUID_CACHE_H_

#include <stdio.h>
#include <stdint.h>
#include <exception>

using std::exception;

class CacheException: public exception {
	const char* _what;
public:
	CacheException(const char* what) :
		_what(what) {
	}

	virtual const char* what() const throw () {
		return _what;
	}
};

class CacheInfo {
public:
	unsigned int id;
	int type;
	const char * type_string;
	int level;
	unsigned int sets;
	unsigned int coherency_line_size;
	unsigned int physical_line_partitions;
	unsigned int ways_of_associativity;
	size_t total_size;
	bool is_fully_associative;
	bool is_self_initializing;

public:
	CacheInfo(unsigned int id = 0);
	void init(unsigned int id);

	static CacheInfo getCacheLevel(int level);

	bool isValid() const;

	void setCacheType(int cache_type);

	void print() const;
};

class i386CpuidCaches {
public:
	CacheInfo* info[32];

public:
	i386CpuidCaches();
	~i386CpuidCaches();
	CacheInfo getCacheLevel(int level);
};

#endif /* CPUID_CACHE_H_ */
