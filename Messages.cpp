/*
 * Messages.cpp
 *
 *  Created on: Aug 31, 2015
 *      Author: liran
 */

#include "Messages.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <sys/types.h>
#include <unistd.h>

using namespace std;

Messages::Messages(const  string& _qname) : qname(_qname){
	createQueue();
}

Messages::Messages(const char* _qname) : qname(_qname){
	createQueue();
}

Messages::~Messages() {
	destroyQueue();
}

void Messages::createQueue() {
	destroyQueue();
	mkfifo(qname.c_str(), 0666);
	queue_fd = -1;
}

void Messages::destroyQueue() {
	unlink(qname.c_str());
}

void Messages::openQueue() {
	if(queue_fd >= 0) {
		closeQueue();
	}
	queue_fd = open(qname.c_str(), O_RDWR);

	if(queue_fd < 0) {
		createQueue();
		queue_fd = open(qname.c_str(), O_RDWR);
	}

	if(queue_fd < 0) {
		throw QueueError("Cannot open queue fifo");
	}
}
void Messages::closeQueue() {
	close(queue_fd);
}

string Messages::getRawMessage() {
	return string(buffer);
}

const vector<string>& Messages::getTokens() {
	return tokens;
}

bool Messages::haveTokens() {
	return tokens.size() > 0;
}

string Messages::popStringToken() {
	if(tokens.size() == 0) {
		throw OutOfTokens();
	}

	string ret = tokens[0];
	tokens.erase(tokens.begin());
	return ret;
}

int Messages::popNumberToken() {
	int res;
	istringstream ( popStringToken() ) >> res;
	return res;
}

ssize_t Messages::readQueueRaw() {
	ssize_t len = 0;
	openQueue();
	len = read(queue_fd, buffer, MAX_BUF);
	closeQueue();
	return len;
}

bool Messages::readQueue() {
	ssize_t len = 0;
	while (len == 0) {
		len = readQueueRaw();
		if (len < 0) {
			std::cout << "Fail to read. Quitting. (" << len << ")" << endl;
			throw QueueError("Fail to read");
		} else if (len == 0) {
			std::cout << "Fail to read with len: " << len << endl;
			continue;
		} else if (buffer[len-1] == '\n') {
			len -= 1;
		}
	}

	buffer[len] = 0;

	istringstream iss(buffer);
	tokens = {istream_iterator<string>{iss},
				istream_iterator<string>{}};

	return true;
}
