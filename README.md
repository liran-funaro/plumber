# plumber

Test the cache leakage issue.

Tested only on Intel(R) Xeon(R) E5-2658 v3 @ 2.20GHz CPUs (Haswell).
It has 30MB, 20-way LLC that supports CAT.

Some of the code is hard-coded for these specific hardware but can be modified to support other hardware.
Lookup `HARD-CODED: Xeon(R) E5-2658 v3` in the code to find such cases.

# Requirements

Download and install cat-driver: https://bitbucket.org/funaro/cat-driver


# License
[GPL](LICENSE.txt)