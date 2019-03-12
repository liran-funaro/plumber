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
