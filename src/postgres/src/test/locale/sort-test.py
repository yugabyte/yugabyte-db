#! /usr/bin/env python

import locale
import sys

locale.setlocale(locale.LC_ALL, "")

if len(sys.argv) != 2:
    sys.stderr.write("Usage: sort.py filename\n")
    sys.exit(1)

infile = open(sys.argv[1], 'r')
list = infile.readlines()
infile.close()

for i in range(0, len(list)):
    list[i] = list[i][:-1]  # chop!

list.sort(key=locale.strxfrm)
print('\n'.join(list))
