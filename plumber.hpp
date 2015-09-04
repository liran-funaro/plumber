/*
 * plumber.hpp
 *
 *  Created on: Sep 3, 2015
 *      Author: liran
 */

#ifndef PLUMBER_HPP_
#define PLUMBER_HPP_

#include <exception>
#include <string>

class PlumberException : public std::exception {
	const std::string _what;
public:
	PlumberException(const char* what) : _what(what) {}
	PlumberException(const std::string& what) : _what(what) {}
	PlumberException(const std::stringstream& what) : _what(what.str()) {}
	virtual const char* what() const throw (){return _what.c_str();}
};

#endif /* PLUMBER_HPP_ */
