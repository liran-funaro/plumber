/*
 * timing.cpp
 *
 *  Created on: Sep 2, 2015
 *      Author: liran
 */

#include "timing.h"

#include <cstdio>
#include <ctime>

timespec timediff(timespec start, timespec end) {
	timespec temp;
	if ((end.tv_nsec - start.tv_nsec) < 0) {
		temp.tv_sec = end.tv_sec - start.tv_sec - 1;
		temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec - start.tv_sec;
		temp.tv_nsec = end.tv_nsec - start.tv_nsec;
	}
	return temp;
}

timespec norm(timespec input, unsigned int norm) {
	timespec res;
	res.tv_sec = 0;
	while (input.tv_sec > norm) {
		res.tv_sec += 1;
		input.tv_sec -= norm;
	}

	res.tv_nsec = (input.tv_nsec + (input.tv_sec * 1000000000)) / norm;

	while (res.tv_nsec > 1000000000) {
		res.tv_sec += 1;
		res.tv_nsec -= 1000000000;
	}

	return res;
}
