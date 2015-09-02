/*
 * timing.h
 *
 *  Created on: Mar 30, 2015
 *      Author: Liran Funaro <fonaro@cs.technion.ac.il>
 */

#ifndef PLUMBER_TIMING_H_
#define PLUMBER_TIMING_H_

#include <sys/time.h>
#include <ctime>
#include <iostream>

#include "lineallocator.hpp"

inline void mfence() __attribute__((always_inline));
inline void mfence() {
  asm volatile("mfence");
}

timespec timediff(timespec start, timespec end);
timespec norm(timespec input, unsigned int norm) ;

inline timespec gettime() {
	timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	return t;
}

inline unsigned long long microsecpassed(struct timeval* t) {
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

#endif /* PLUMBER_TIMING_H_ */
