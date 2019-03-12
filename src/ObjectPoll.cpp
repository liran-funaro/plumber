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
#include "ObjectPoll.h"

ObjectPoll::ObjectPoll(unsigned long objectSize, unsigned long pollSize) : objectSize(objectSize),  pollSize(pollSize) {
	poll = (char*) mmap(0, pollSize, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (PAGE_MASK(poll) != 0) {
		throw ObjectPollException("Not page aligned");
	}

	pollGCPos = poll;
	pollPos = poll;
	pollEnd = poll + pollSize / sizeof(*poll);

	usePageOffset = false;
	pageOffset = 0;
	freedPages = 0;
}

ObjectPoll::~ObjectPoll() {
	freeArea(poll, pollSize);
}

void ObjectPoll::freeArea(void* p, unsigned long size) {
	madvise(p, size, MADV_DONTNEED);
}

void ObjectPoll::GC() {
	for (char* pollIter = pollGCPos; pollIter < pollPos; pollIter += PAGE_SIZE) {
		auto buff = reinterpret_cast<unsigned long*>(pollIter);
		bool isCleared = true;

		for (unsigned int i = 0; i < PAGE_SIZE / sizeof(*buff); ++i) {
			if (buff[i] != 0) {
				isCleared = false;
				break;
			}
		}

		if (isCleared) {
			freeArea(pollIter, PAGE_SIZE);
			freedPages += 1;
		}
	}

	pollGCPos = reinterpret_cast<char*>(PAGE_FRAME_MASK(pollPos));
}

unsigned long ObjectPoll::getTotalAllocatedPoll() {
	return PTR_TO_ADDR(pollPos) - PTR_TO_ADDR(poll) - freedPages * PAGE_SIZE;
}

void ObjectPoll::setPageOffset(void* p) {
	if (p == NULL) {
		usePageOffset = false;
		pageOffset = 0;
	} else {
		usePageOffset = true;
		pageOffset = PAGE_MASK(p);
	}
}

void ObjectPoll::deleteObject(void *p) {
	memset(p, 0, objectSize);
}

void* ObjectPoll::newObject() {
	if (pollPos+objectSize > pollEnd) {
		throw ObjectPollException("Out of Poll");
	}

//		if(PAGE_MASK(pollPos) == 0) {
//			if(PTR_TO_ADDR(pollPos) >= 0x00800000000L) {
//				memset((ptr)pollPos, 0, PAGE_SIZE);
//				pollPos += PAGE_SIZE/LINE_SIZE;
//			}
//		}

	if (usePageOffset) {
		unsigned int curOffset = PAGE_MASK(pollPos);
		while (curOffset != pageOffset) {
			deleteObject(pollPos);
			pollPos += objectSize;
			curOffset = PAGE_MASK(pollPos);
		}
	}

	auto ret = pollPos;
	pollPos += objectSize;

	return ret;
}

unsigned long ObjectPoll::calculatePhyscialAddr(void* ptr) {
	// https://shanetully.com/2014/12/translating-virtual-addresses-to-physcial-addresses-in-user-space/

	unsigned long virtAddress = reinterpret_cast<unsigned long>(ptr);
	unsigned long pageOffset = virtAddress % PAGE_SIZE;

	// Open the pagemap file for the current process
	FILE *pagemap = fopen("/proc/self/pagemap", "rb");

	// Seek to the page that the buffer is on
	unsigned long pageTableOffset = (virtAddress / PAGE_SIZE) * PAGEMAP_LENGTH;
	if (fseek(pagemap, pageTableOffset, SEEK_SET) != 0) {
		throw ObjectPollException("Failed to seek pagemap to proper location");
	}

	// The page frame number is in bits 0-54 so read the first 7 bytes and clear the 55th bit
	unsigned long page_frame_number = 0;
	if (fread(&page_frame_number, 1, PAGEMAP_LENGTH - 1, pagemap)
			!= PAGEMAP_LENGTH - 1) {
		throw ObjectPollException("Failed to read page frame number");
	}

	fclose(pagemap);

	page_frame_number &= 0x7FFFFFFFFFFFFF;

	return (page_frame_number << PAGE_SHIFT) | pageOffset;
}
