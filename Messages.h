/*
 * Messages.h
 *
 *  Created on: Aug 31, 2015
 *      Author: liran
 */

#ifndef PLUMBER_MESSAGES_H_
#define PLUMBER_MESSAGES_H_

#include <string>
#include <vector>
#include <exception>
using namespace std;

class OutOfTokens : public exception {
public:
	OutOfTokens() {}
	virtual const char* what() const throw () {
		return "";
	}
};

class QueueError : public exception {
	const char* _what;
public:
	QueueError(const char* what) : _what(what) {}
	virtual const char* what() const throw () {
		return _what;
	}
};

class UnknownOperation : public exception {
	string _op;
public:
	UnknownOperation(const string& op) : _op(op) {}
	virtual const char* what() const throw () {
		return "Unknown operation";
	}

	virtual const string& op() const throw () {
		return _op;
	}
};

class Messages {
	enum {MAX_BUF=1<<12};

	const string qname;

	int queue_fd;
	char buffer[MAX_BUF + 1];
	vector<string> tokens;
public:
	Messages(const string& qname);
	Messages(const char* qname);
	~Messages();

public:
	bool readQueue();

public:
	string getRawMessage();

	const vector<string>& getTokens();
	bool haveTokens();
	string popStringToken();
	int popNumberToken();

private:
	void createQueue();
	void destroyQueue();

	void openQueue();
	void closeQueue();

	ssize_t readQueueRaw();
};

#endif /* PLUMBER_MESSAGES_H_ */
