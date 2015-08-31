/*
 * deamon.cpp
 *
 *  Created on: Aug 31, 2015
 *      Author: liran
 */
/*
 * deamon.h
 *
 *  Created on: Aug 31, 2015
 *      Author: liran
 */

#include "deamon.h"

#include <signal.h>   //signal(3)
#include <stdio.h>    //printf(3)
#include <stdlib.h>   //exit(3)
#include <sys/file.h> //flock
#include <sys/stat.h> //umask(3)
#include <syslog.h>   //syslog(3), openlog(3), closelog(3)
#include <unistd.h>   //fork(3), chdir(3), sysconf(3)
#include <iostream>

using namespace std;

int daemonize(const char* name, const char* infile, const char* outfile) {
	if (!name) {
		name = "medaemon";
	}
	if (!infile) {
		infile = "/dev/null";
	}
	if (!outfile) {
		outfile = "/dev/null";
	}

	//printf("%s %s %s %s\n",name,path,outfile,infile);
	pid_t child;
	//fork, detach from process group leader
	if ((child = fork()) < 0) { //failed fork
		syslog(LOG_ERR | LOG_USER, "First fork failed: %m");
		exit(EXIT_FAILURE);
	}
	if (child > 0) { //parent
		exit(EXIT_SUCCESS);
	}
	if (setsid() < 0) { //failed to become session leader
		fprintf(stderr, "error: failed setsid\n");
		syslog(LOG_ERR | LOG_USER, "Failed setsid: %m");
		exit(EXIT_FAILURE);
	}

	//catch/ignore signals
	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	//fork second time
	if ((child = fork()) < 0) { //failed fork
		syslog(LOG_ERR | LOG_USER, "Second fork failed: %m");
		exit(EXIT_FAILURE);
	}
	if (child > 0) { //parent
		exit(EXIT_SUCCESS);
	}

	//new file permissions
	umask(0);
	//change to path directory
	auto ret = chdir("/");

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	//Close all open file descriptors
	int fd;
	for (fd = sysconf(_SC_OPEN_MAX); fd > 0; --fd) {
		close(fd);
	}

	//reopen stdin, stdout, stderr
	stdin = fopen(infile, "r");   //fd=0
	stdout = fopen(outfile, "w+");  //fd=1
	if (dup(1) < 0) {
		syslog(LOG_ERR | LOG_USER, "Unable to dup output descriptor: %m");
		return 1;
	}
	stderr = stdout;

	setvbuf ( stdout , NULL , _IONBF , 1 );

	flock(0, LOCK_SH);

	//open syslog
	openlog(name, LOG_PID, LOG_DAEMON);
	syslog(LOG_NOTICE,"Deamon started");

	return ret;
}

void finilize_deamon() {
	closelog();
}
