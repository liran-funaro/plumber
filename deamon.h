/*
 * deamon.h
 *
 *  Created on: Aug 31, 2015
 *      Author: liran
 */

#ifndef PLUMBER_DEAMON_H_
#define PLUMBER_DEAMON_H_

int daemonize(const char* name, const char* infile, const char* outfile);
void finilize_deamon();

#endif /* PLUMBER_DEAMON_H_ */
