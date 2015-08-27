/*
 * timing.h
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

#ifndef TIMING_H_
#define TIMING_H_

#include <sys/time.h>
#include "pageallocator.h"

timespec diff(timespec start, timespec end) {
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

template<unsigned int LINE_SIZE>
void timeTouch(CacheLineAllocator<LINE_SIZE>& x) {
	timespec ts, te;
	for (unsigned int i = 0; i < 10; i++) {
		clock_gettime(CLOCK_REALTIME, &ts);
		for (unsigned int j = 0; j < 1000; j++) {
			for (unsigned int way = 0; way < 12; way++) {
				x.touchWay(way);
			}
		}
		clock_gettime(CLOCK_REALTIME, &te);
		timespec sumTime = diff(ts, te);
		timespec totalTime = norm(sumTime, 1000);
		timespec wayTime = norm(totalTime, 12);
		cout << dec << "Iteration: " << i << " - Total-Time: "
				<< totalTime.tv_sec << " sec " << totalTime.tv_nsec << " nsec"
				<< " - Way-Time: " << wayTime.tv_sec << " sec "
				<< wayTime.tv_nsec << " nsec" << endl;
	}
}

unsigned long long microsecpassed(struct timeval* t) {
	struct timeval now, diff;
	gettimeofday(&now, NULL);
	timersub(&now, t, &diff);
	return (diff.tv_sec * 1000 * 1000)  + diff.tv_usec;
}

#if defined(__i386__)

inline unsigned long long rdtsc() {
  unsigned int lo, hi;
  __asm__ volatile (
     "cpuid \n"
     "rdtsc"
   : "=a"(lo), "=d"(hi) /* outputs */
   : "a"(0)             /* inputs */
   : "%ebx", "%ecx");     /* clobbers*/
  return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}
#elif 0
static inline unsigned long long rdtsc(void) {
    unsigned long long hi, lo;
    __asm__ __volatile__(
            "xorl %%eax, %%eax;\n\t"
            "push %%ebx;"
            "cpuid\n\t"
            ::
            :"%eax", "%ebx", "%ecx", "%edx");
    __asm__ __volatile__(
            "rdtsc;"
            : "=a" (lo),  "=d" (hi)
            ::);
    __asm__ __volatile__(
            "xorl %%eax, %%eax; cpuid;"
            "pop %%ebx;"
            ::
            :"%eax", "%ebx", "%ecx", "%edx");

    return (unsigned long long)hi << 32 | lo;
}

#elif 0
static inline unsigned long long rdtsc(void)
{
  unsigned long long int x;
     __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
     return x;
}
#elif defined(__x86_64__)
/*static inline unsigned long long rdtsc(void) {
    unsigned long long hi, lo;
    __asm__ __volatile__(
            "xorl %%eax, %%eax;\n\t"
            "push %%rbx;"
            "cpuid\n\t"
            ::
            :"%rax", "%rbx", "%rcx", "%rdx");
    __asm__ __volatile__(
            "rdtsc;"
            : "=a" (lo),  "=d" (hi)
            ::);
    __asm__ __volatile__(
            "xorl %%eax, %%eax; cpuid;"
            "pop %%rbx;"
            ::
            :"%rax", "%rbx", "%rcx", "%rdx");

    return (unsigned long long)hi << 32 | lo;
}
#elif 0
*/
static inline unsigned long long rdtsc(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

#elif defined(__powerpc__)


static __inline__ unsigned long long rdtsc(void)
{
  unsigned long long int result=0;
  unsigned long int upper, lower,tmp;
  __asm__ volatile(
                "0:                  \n"
                "\tmftbu   %0           \n"
                "\tmftb    %1           \n"
                "\tmftbu   %2           \n"
                "\tcmpw    %2,%0        \n"
                "\tbne     0b         \n"
                : "=r"(upper),"=r"(lower),"=r"(tmp)
                );
  result = upper;
  result = result<<32;
  result = result|lower;

  return(result);
}

#endif

#endif /* TIMING_H_ */
