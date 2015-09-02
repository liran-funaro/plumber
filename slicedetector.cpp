/*
 * slicedetector.cpp
 *
 *  Created on: Sep 2, 2015
 *      Author: liran
 */

#include <cstdlib>
#include <iostream>

void flushLLC(bool verbose) {
//	 asm volatile("wbinvd");
	if(verbose) {
		std::cout << "[FLUSH] Flushing LLC... ";
	}
	int ret = system("sudo cgexec -g cpuset:plumber -g intel_rdt:flushllc sudo /home/fonaro/workspace/flush_llc/flushllc");
	ret += 1;
	if(verbose) {
		std::cout << "[SUCCESS]" << std::endl;
	}
}


