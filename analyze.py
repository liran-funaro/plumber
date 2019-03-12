"""
Author: Liran Funaro <liran.funaro@gmail.com>

Copyright (C) 2006-2018 Liran Funaro

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
"""
import itertools, sys, pprint, glob
from copy import copy

mem_ranges = {
 0: (0x00000000000, 0x001FFFFFFFF),
 1: (0x00200000000, 0x003FFFFFFFF),
 2: (0x00400000000, 0x005FFFFFFFF),
 3: (0x005FFFFFFFF, 0x007FFFFFFFF),
 4: (0x00800000000, 0x009FFFFFFFF),
 5: (0x00A00000000, 0x00BFFFFFFFF),
 6: (0x00C00000000, 0x00DFFFFFFFF),
 7: (0x00E00000000, 0x00FFFFFFFFF)
 }

def in_range(mem_addr, mem_range):
 	start, end = mem_range
 	return mem_addr >= start and mem_addr <= end

def get_range(mem_addr):
	for i, mem_range in mem_ranges.iteritems():
		if in_range(mem_addr, mem_range):
			return i

def get_all_ranges(mem_addresses):
	all_ranges = set()
	for a in mem_addresses:
		all_ranges.add( get_range(a) )

	return sorted(all_ranges)

def all_subsets(iterable):
	for L in range(0, len(iterable)+1):
 		for subset in itertools.combinations(iterable, L):
			yield subset

def hash_func(addr, bits):
	hash_val = 0;
	for b in bits:
		hash_val ^= (addr >> b) & 1;

	return hash_val

def multi_hash(addr, x_bits):
	res = 0
	for i, bits in enumerate(x_bits):
		res |= hash_func(addr, bits) << i

	return res

def xor_and_or_hash_func(addr, xor_bits, and_bits = [], or_bits = []):
	xor_val = 0;
	and_val = 1;
	or_val = 0;

	for b in xor_bits:
		xor_val ^= (addr >> b) & 1

	for b in and_bits:
		and_val &= (addr >> b) & 1

	for b in or_bits:
		or_val |= (addr >> b) & 1

	if len(or_bits) == 0:
		or_val = 1

	return (xor_val & and_val) & or_val

def xor_and_or_hash_multi_hash(addr, multi_bits):
	res = 0
	for i, (xor_bits, and_bits, or_bits) in enumerate(multi_bits):
		res |= xor_and_or_hash_func(addr, xor_bits, and_bits, or_bits) << i

	return res

def test_all_bits(address, hash_function):
	first_addr = next(iter(address))
	cur_hash = hash_function(first_addr)

	for a in address:
		if hash_function(a) != cur_hash:
			# print "Fail in address:", bin(a), hex(a)
			return False

	return True

def find_runs(bitmask):
	res = []
	run_start=0
	pos = 0
	cur = bitmask & 1

	while bitmask != 0:
		while cur == (bitmask & 1):
			pos += 1
			bitmask = bitmask >> 1

		res.append( (cur, run_start, pos-1) )
		run_start = pos
		cur = bitmask & 1

	return res

def read_addresses(file_name):
	with open(file_name, "r") as f:
		address = f.read().splitlines()
		return [ int(a, 16) for a in address ]

def read_dataset(*filenames):
	address_datasets = []
	for file_name in filenames:
		address = read_addresses(file_name)
		address_datasets.append(address)

	return address_datasets

def join_dataset(address_datasets):
	res = []
	for a in address_datasets:
		res += a

	return res

def find_set_unset(address):
	and_value = ~0
	for a in address:
		and_value &= a

	or_value = 0
	for a in address:
		or_value |= a

	and_runs = find_runs(and_value)
	or_runs  = find_runs(or_value)
	ones  = [ (s,e) for b, s, e in and_runs if b == 1 ]
	zeros = [ (s,e) for b, s, e in or_runs  if b == 0 ]

	i=0
	j=0

	RED = "\033[41m%s\033[0m"
	RED_Q = RED % "?"

	res = []
	while i < len(ones) or j < len(zeros):
		next_pos = 0 if len(res) == 0 else res[-1][2]+1

		if i < len(ones) and j < len(zeros):
			if ones[i][0] < zeros[j][0]:
				if next_pos != ones[i][0]:
					res.append( (RED_Q, next_pos, ones[i][0]-1) )
				res.append( (1, ones[i][0], ones[i][1]) )
				i += 1
			else:
				if next_pos != zeros[j][0]:
					res.append( (RED_Q, next_pos, zeros[j][0]-1) )
				res.append( (0, zeros[j][0], zeros[j][1]) )
				j += 1
		elif i < len(ones):
			if next_pos != ones[i][0]:
				res.append( (RED_Q, next_pos, ones[i][0]-1) )
			res.append( (1, ones[i][0], ones[i][1]) )
			i += 1
		else:
			if next_pos != zeros[j][0]:
				res.append( (RED_Q, next_pos, zeros[j][0]-1) )
			res.append( (0, zeros[j][0], zeros[j][1]) )
			j += 1

	next_pos = 0 if len(res) == 0 else res[-1][2]+1
	and_last_pos = and_runs[-1][2] if len(and_runs) > 0 else 0
	or_last_pos = or_runs[-1][2] if len(or_runs) > 0 else 0
	last_pos = max(and_last_pos, or_last_pos)
	if last_pos >= next_pos:
		res.append( (RED_Q, next_pos, last_pos) )

	# print "Set   in all: 0x{0:12x}, BIN: 0b{0:48b}".format(and_value)
	# print "Unset in all: 0x{0:12x}, BIN: 0b{0:48b}".format(or_value)
	# print ", ".join([ "%s: %s-%s" % item for item in res])
	return res

def detect_subsets(bits_subsets):
	res = []

	bits_subsets_sets = [ set(k) for k in bits_subsets]

	for i,k in enumerate(bits_subsets_sets):
		issubset = False
		k_set = set(k)

		for j,sk in enumerate(bits_subsets_sets):
			if i == j:
				continue

			if k.issubset(sk):
				issubset = True
				break

		if not issubset:
			res.append( bits_subsets[i] )

	return res

def validate_dataset(filenames, address_datasets):
	address_sets = [ set(a) for a in address_datasets ]

	res = []
	for i, x_address in enumerate(address_sets):
		for j, y_address in enumerate(address_sets):
			if j >= i:
				break

			u = x_address.intersection(y_address)

			x_address.difference_update(u)
			y_address.difference_update(u)

	return address_sets

def analyze(*filenames):
	address_datasets = read_dataset(*filenames)
	valid_address_datasets = validate_dataset(filenames, address_datasets)
	for f, a, va in zip(filenames, address_datasets, valid_address_datasets):
		print f,":",len(a),"->",len(va), "- diff:",set(a).difference(va)

	address_datasets = valid_address_datasets
	all_address = join_dataset(address_datasets)
	print "Total address count:", len(all_address)

	# datasets_marks = [ find_set_unset(a) for a in address_datasets ]

	# for f_name, address in zip(filenames, address_datasets):
	# 	print f_name,":\t", get_all_ranges(address)

	# return

	max_bit = 0
	for f_name, address in zip(filenames, address_datasets):
		su = find_set_unset(address)
		print f_name,":\t", get_all_ranges(address), ":\t", ", ".join([ "%s: %s-%s" % item for item in su])
		for _,_,e in su:
			max_bit = max(max_bit, e)

	all_bits = range(17,e+1)

	test_bits = all_subsets(all_bits)
	# test_bits = known_bits

	found_xor_bits = []

	for xor_bits_subset in test_bits:
		if len(xor_bits_subset) == 0:
			continue

		success = True

		cur_hash_func = lambda addr: hash_func(addr, xor_bits_subset)

		for address in address_datasets:
			if not test_all_bits(address, cur_hash_func):
				success = False
				break
		
		if success:
			found_xor_bits.append( xor_bits_subset )

	found_bits_no_dup = detect_subsets(found_xor_bits)

	print
	print "Found %s bits subsets:" % len(found_xor_bits)
	pprint.pprint(found_xor_bits)

	print "Found %s bits without subset:" % len(found_bits_no_dup)
	pprint.pprint(found_bits_no_dup)

	# Sanity check
	for i, x_bits in enumerate(found_xor_bits):
		cur_hash_func = lambda addr: hash_func(addr, x_bits)
		if test_all_bits(all_address, cur_hash_func):
			print "[SANITY CHECK FAILED]", found_xor_bits[i],"(%s) passed for all" % i

	print
	# bin_format = "0b{0:%sb}" % len(found_bits)
	all_hash = set()
	# all_hash_funcs = [lambda addr: hash_func(addr, x_bits) for x_bits in found_xor_bits]
	real_hash_func = lambda addr: multi_hash(addr, found_xor_bits)

	for f_name, address in zip(filenames, address_datasets):
		first_addr = next(iter(address))
		h = real_hash_func(first_addr)
		all_hash.add(h)
		# print f_name,":\t",bin_format.format(h),"=",h

	print "All hashes:", sorted(all_hash), "(%s)" % len(all_hash)

if __name__ == "__main__":
	if len(sys.argv) == 1:
		files = glob.glob("data/*.txt")
	else:
		files = sys.argv[1:]
	analyze(*files)

	# high_hashes = [6, 9, 21, 26]

	# all_hash = set()

	# for i in xrange(1<<34, 1<<36, 1<<12):
	# 	h = multi_hash(i, known_bits)
	# 	all_hash.add(h)

	# 	if len(all_hash) >= 5:
	# 		break

	# print "All hashes:", sorted(all_hash)