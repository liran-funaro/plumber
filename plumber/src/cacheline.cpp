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
#include "cacheline.hpp"

ObjectPoll* CacheLine::poll;
map<unsigned long, unsigned int> CacheLine::oldAddressMap;
int CacheLine::pollute_dummy;

CacheLine::CacheLine(unsigned int lineSize, unsigned int _inSliceSetCount) {
	if (sizeof(*this) > lineSize) {
		throw CacheLineException(this, "Object is bigger then line size");
	}

	unsigned long virtualAddress = PTR_TO_ADDR(this);
	if (virtualAddress % lineSize != 0) {
		throw CacheLineException(this, "Not aligned to line size");
	}

	mlock(this, sizeof(*this));

	unsigned int roundedSetCount;
	for (roundedSetCount=1; roundedSetCount<_inSliceSetCount; roundedSetCount*=2 );
	if(roundedSetCount != _inSliceSetCount) {
		throw CacheLineException(this, "In slice set count must be a power of 2");
	}

	next = NULL;
	physcialAddr = calculatePhyscialAddr();

	lineRelativePhyscialAddress = physcialAddr / lineSize;

	inSliceSetCount = _inSliceSetCount;
	cacheSlice = -1;

	calculateSet();
}

void CacheLine::calculateSet() {
	lineSet = getInSliceSet()
		| (cacheSlice < 0 ? 0 : (cacheSlice * inSliceSetCount));
}

void CacheLine::validatePhyscialAddr() const {
	if(physcialAddr != calculatePhyscialAddr()) {
		CacheLineException(this, "Physical address changed!");
	}
}

void CacheLine::validateAll() const {
	const CacheLine* curLine = this;

	do {
		curLine->validatePhyscialAddr();
		curLine = curLine->getNext();
	} while(curLine != this);
}

void CacheLine::setCacheSlice(unsigned long _cacheSlice) {
	if(cacheSlice < 0){ // || cacheSlice == cacheSlice) {
		cacheSlice = (int)_cacheSlice;
	} else {
		throw CacheSliceResetException(this, cacheSlice, cacheSlice);
	}

	calculateSet();
}

void CacheLine::resetCacheSlice() {
	cacheSlice = -1;
	calculateSet();
}


unsigned long CacheLine::calculatePhyscialAddr() const {
	return ObjectPoll::calculatePhyscialAddr((void*)this);
}

/*********************************************************************************************
 * Pollute Control
 *********************************************************************************************/
void CacheLine::flushSets() {
	ptr curline = this;
	do {
		ptr nextline = curline->getNext();
		curline->flushFromCache();
		curline = nextline;
	} while (curline != this);
}

void CacheLine::polluteSets(CacheLine::arr partitionsArray, unsigned long partitionsCount,
		volatile bool& continueFlag, bool disableInterupts) {
	continueFlag = true;

	if(disableInterupts) {
		iopl(3);
		__asm__ __volatile__("cli");
	}

	while (continueFlag) {
		for(unsigned long i=0; i < partitionsCount; i++) {
			CacheLine::ptr a = *(volatile CacheLine::ptr*) &partitionsArray[i];
			partitionsArray[i] = a->next;
		}
	}

	if(disableInterupts) {
		__asm__ __volatile__("sti");
	}
}

/*********************************************************************************************
 * Poll Control
 *********************************************************************************************/
void CacheLine::allocatePoll(unsigned int setLineSize) {
	if(poll == NULL) {
		poll = new ObjectPoll(setLineSize, POLL_SIZE);
	}
}

void CacheLine::GC() {
	if(poll != NULL) {
		poll->GC();
	}
}

unsigned long CacheLine::getTotalAllocatedPoll() {
	if(poll != NULL) {
		return poll->getTotalAllocatedPoll();
	}

	return 0;
}

void CacheLine::setPageOffset(ptr p) {
	if(poll != NULL) {
		poll->setPageOffset(p);
	}
}

void CacheLine::operator delete(void *p) {
	if(poll != NULL) {
		poll->deleteObject(p);
	}
}

void* CacheLine::operator new(__attribute__((unused)) size_t size) {
	if(poll == NULL) {
		return NULL;
	}

	return poll->newObject();
}

/*********************************************************************************************
 * CacheLine::lst
 *********************************************************************************************/
void CacheLine::lst::insertBack(ptr l) {
	if (l == NULL) {
		return;
	}

	if (_last != NULL) {
		_last->setNext(l);
	} else {
		_first = l;
	}

	_last = l;
	_length += 1;
	_last->setNext(_first);
}

void CacheLine::lst::insertBack(const lst& l) {
	if (_last != NULL) {
		_last->setNext(l.front());
	} else {
		_first = l.front();
	}

	_last = l.back();

	// We already added the first line
	_length += l.size();
	_last->setNext(_first);
}

CacheLine::ptr CacheLine::lst::popFront() {
	ptr ret = front();
	if(ret != NULL) {
		_first = ret->getNext();
		if(_first == ret) {
			_first = NULL;
			_last = NULL;
		}

		if(_last != NULL) {
			_last->setNext(_first);
		}

		_length -= 1;
	}
	return ret;
}

vector<CacheLine::lst> CacheLine::lst::partition(unsigned int size) {
	vector<lst> ret(size);

	unsigned int pos = 0;
	for(ptr l = popFront(); l != NULL; l = popFront()) {
		ret[pos].insertBack(l);
		pos = (pos+1) % size;
	}

	return ret;
}

void CacheLine::lst::validate() {
	CacheLine::ptr curline = front();

	CacheLine::uset lineSet;
	unsigned long count = 0;

	do {
		lineSet.insert(curline);
		count += 1;
		curline = curline->getNext();
	} while (curline != front());

	if (count != lineSet.size()) {
		stringstream ss;
		ss << "Repeating items in list. List length is " << count << ", but only "
				<< lineSet.size() << " unique items.";
		throw CacheListException(ss);
	}

	if(count != size()) {
		throw CacheListException("List length is not correct.");
	}

	front()->validateAll();
}

/*********************************************************************************************
 * Backup
 *********************************************************************************************/
void CacheLine::print() {
	cout << "VIRT PAGE: 0x" << hex << reinterpret_cast<unsigned long>(this)
				<< " - PHYS PAGE: 0x" << hex << calculatePhyscialAddr() << " - SET: 0x"
				<< getSet() << endl;
}

unsigned int CacheLine::get_cache_slice() {
	// On a 4-core machine, the CPU's hash function produces a 2-bit
	// cache slice number, where the two bits are defined by "h1" and
	// "h2":
	//
	// h1 function:
	//   static const int bits[] = { 18, 19, 21, 23, 25, 27, 29, 30, 31 };
	// h2 function:
	//   static const int bits[] = { 17, 19, 20, 21, 22, 23, 24, 26, 28, 29, 31 };
	//
	// This hash function is described in the paper "Practical Timing
	// Side Channel Attacks Against Kernel Space ASLR".
	//
	// On a 2-core machine, the CPU's hash function produces a 1-bit
	// cache slice number which appears to be the XOR of h1 and h2.

	// XOR of h1 and h2:

	static const int h1_bits[] = { 16, 17, 21, 23, 24, 27, 33, 34 };
	static const int h2_bits[] = { 16, 18, 19, 20, 22, 24, 25, 30, 32, 33,
			34 };
	static const int h3_bits[] = { 16, 17, 18, 19, 20, 21, 22, 23, 25, 27,
			30, 32 };
	//		static const int h4_bits[] = {17, 18, 19, 20, 21, 22, 23, 25, 27, 30, 32, 36};
	//		static const int h5_bits[] = {18, 19, 20, 22, 24, 25, 30, 32, 33, 34, 35, 36};

	unsigned int hash = 0;
	hash |= (getHashValue(h1_bits, ARRAY_LENGTH(h1_bits)) << 0);
	hash |= (getHashValue(h2_bits, ARRAY_LENGTH(h2_bits)) << 1);
	hash |= (getHashValue(h3_bits, ARRAY_LENGTH(h3_bits)) << 2);
	//		hash |= (getHashValue(h4_bits, ARRAY_LENGTH(h4_bits)) << 3);
	//		hash |= (getHashValue(h5_bits, ARRAY_LENGTH(h5_bits)) << 4);

	return hash;
}

unsigned int CacheLine::getHashValue(const int *bits, unsigned int bitsCount) {
	unsigned int hash = 0;
	for (unsigned int i = 0; i < bitsCount; i++) {
		hash ^= (unsigned int) ((physcialAddr >> bits[i]) & 1);
	}

	return hash;
}

unsigned long CacheLine::stringToAddr(string& str) {
	unsigned long addr;
	std::stringstream hexss;
	hexss << std::hex << str;
	hexss >> addr;
	return addr;
}

int CacheLine::getCacheSliceFromFile() {
	if (oldAddressMap.size() == 0) {
		ifstream datafile;
		datafile.open("cacheline.txt");

		while (datafile) {
			string s;
			if (!getline(datafile, s))
				break;
			if (s[0] == '#') {
				continue;
			}

			istringstream ss(s);
			vector<string> record;

			while (ss) {
				string s;
				if (!getline(ss, s, ';'))
					break;
				record.push_back(s);
			}

			unsigned long curSlice = stringToAddr(record[1]);
			unsigned long curAddr = stringToAddr(record[2]);
			oldAddressMap[curSlice] = curAddr;
		}
	}

	auto it = oldAddressMap.find(getPhysicalAddr());
	if (it != oldAddressMap.end()) {
		std::cout << endl << "FOUND ADDR" << endl;
		return it->second;
	}

	return -1;
}
