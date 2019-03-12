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
#ifndef PLUMBER_CPUID_CACHE_H_
#define PLUMBER_CPUID_CACHE_H_

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

	unsigned int cache_slices;

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

#endif /* PLUMBER_CPUID_CACHE_H_ */
