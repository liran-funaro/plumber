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
#include <iostream>

#include "cpuid_cache.h"
using namespace std;

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(*(arr)))

static inline void cpuid(uint32_t a, uint32_t c, uint32_t& eax, uint32_t& ebx, uint32_t& ecx, uint32_t&edx) {
	eax = a;
	ecx = c;

	__asm__ (
			"cpuid" // call i386 cpuid instruction
			: "+a" (eax)// contains the cpuid command code, 4 for cache query
			, "=b" (ebx)
			, "+c" (ecx)// contains the cache id
			, "=d" (edx)
	);
}

//static inline uint32_t mask_bits(uint32_t reg, unsigned int start, unsigned int end) {
//	reg >>= start;
//	reg &= (1<<(end-start+1))-1;
//	return reg;
//}

CacheInfo::CacheInfo(unsigned int id) {
	init(id);
}

void CacheInfo::init(unsigned int id) {
	this->id = id;

	uint32_t eax, ebx, ecx, edx;
	cpuid(4, id, eax, ebx, ecx, edx); // 4: cache-info, id: cache-id

	// taken from http://download.intel.com/products/processor/manual/325462.pdf Vol. 2A 3-149
	setCacheType(eax & 0x1F);
	if (!isValid()) // end of valid cache identifiers
		return;

	level = (eax >>= 5) & 0x7;

	is_self_initializing = (eax >>= 3) & 0x1; // does not need SW initialization
	is_fully_associative = (eax >>= 1) & 0x1;

	// taken from http://download.intel.com/products/processor/manual/325462.pdf 3-166 Vol. 2A
	// ebx contains 3 integers of 10, 10 and 12 bits respectively
	sets = ecx + 1;
	coherency_line_size = (ebx & 0xFFF) + 1;
	physical_line_partitions = ((ebx >>= 12) & 0x3FF) + 1;
	ways_of_associativity = ((ebx >>= 10) & 0x3FF) + 1;

	// Total cache size is the product
	total_size = ways_of_associativity * physical_line_partitions
			* coherency_line_size * sets;

	// HARD-CODED: Xeon(R) E5-2658 v3
	// TODO: detect using CPUID
	cache_slices = 12;
}

CacheInfo CacheInfo::getCacheLevel(int level) {
	unsigned int i = 0;
	CacheInfo result(0);
	while (result.isValid() && result.level != level) {
		i++;
		result.init(i);
	}

	return result;
}

bool CacheInfo::isValid() const {
	return type != 0;
}

void CacheInfo::setCacheType(int cache_type) {
	this->type = cache_type;
	switch (cache_type) {
	case 1:
		type_string = "Data Cache";
		break;
	case 2:
		type_string = "Instruction Cache";
		break;
	case 3:
		type_string = "Unified Cache";
		break;
	default:
		type_string = "Unknown Type Cache";
		break;
	}
}

void CacheInfo::print() const {
	printf("Cache ID %d:\n"
			" - Level: %d\n"
			" - Type: %s\n"
			" - Sets: %d\n"
			" - System Coherency Line Size: %d bytes\n"
			" - Physical Line partitions: %d\n"
			" - Ways of associativity: %d\n"
			" - Total Size: %zu bytes (%zu kb)\n"
			" - Is fully associative: %s\n"
			" - Is Self Initializing: %s\n"
			" - Number of cache slices: %d\n",
			id, level, type_string, sets,
			coherency_line_size, physical_line_partitions,
			ways_of_associativity, total_size, total_size >> 10,
			is_fully_associative ? "true" : "false",
			is_self_initializing ? "true" : "false",
			cache_slices);
}

i386CpuidCaches::i386CpuidCaches() {
	for (unsigned int i = 0; i < ARRAY_SIZE(info); i++) {
		info[i] = NULL;
	}

	for (unsigned int i = 0; i < ARRAY_SIZE(info); i++) {
		info[i] = new CacheInfo(i);
		if (!info[i]->isValid()) {
			delete info[i];
			info[i] = NULL;
		}
	}
}

i386CpuidCaches::~i386CpuidCaches() {
	for (unsigned int i = 0; i < ARRAY_SIZE(info); i++) {
		if (info[i] != NULL) {
			delete info[i];
			info[i] = NULL;
		}
	}
}

CacheInfo i386CpuidCaches::getCacheLevel(int level) {
	unsigned int i;
	for (i = 0; i < sizeof(info); i++) {
		if (info[i] == NULL || info[i]->level == level) {
			break;
		}
	}

	if (i == sizeof(info) || info[i] == NULL) {
		throw CacheException("No cache with this level");
	}

	return *info[i];
}
