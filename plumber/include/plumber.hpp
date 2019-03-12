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
