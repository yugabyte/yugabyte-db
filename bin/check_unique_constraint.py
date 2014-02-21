#!/usr/bin/env python

import argparse, collections, psycopg2, os, subprocess, sys, tempfile

parser = argparse.ArgumentParser(description="This script is used to check that all rows in a partition set are unique for the given columns. Since unique constraints are not applied across partition sets, this cannot be enforced within the database. This script can be used as a monitor to ensure uniquness. If any unique violations are found, the values, along with a count of each, are output.")
parser.add_argument('-p', '--parent', required=True, help="Parent table of the partition set to be checked")
parser.add_argument('-l', '--column_list', required=True, help="Comma separated list of columns that make up the unique constraint to be checked")
parser.add_argument('-c','--connection', default="host=localhost", help="""Connection string for use by psycopg. Defaults to "host=localhost".""")
parser.add_argument('-t', '--temp', help="Path to a writable folder that can be used for temp working files. Defaults system temp folder.")
parser.add_argument('--psql', help="Full path to psql binary if not in current PATH")
parser.add_argument('--simple', action="store_true", help="Output a single integer value with the total duplicate count. Use this for monitoring software that requires a simple value to be checked for.")
parser.add_argument('-q', '--quiet', action="store_true", help="Suppress all output unless there is a constraint violation found.")
args = parser.parse_args()

if args.temp == None:
    tmp_copy_file = tempfile.NamedTemporaryFile(prefix="partman_constraint")
else:
    tmp_copy_file = tempfile.NamedTemporaryFile(prefix="partman_constraint", dir=args.temp)

fh = open(tmp_copy_file.name, 'w')
conn = psycopg2.connect(args.connection)
cur = conn.cursor()
if not args.quiet:
    print "Dumping out column data to temp file..."
cur.copy_to(fh, args.parent, sep=",", columns=args.column_list.split(","))
conn.close()
fh.close()

total_count = 0
if not args.quiet:
    print "Checking for dupes..."
with open(tmp_copy_file.name) as infile:
    counts = collections.Counter(l.strip() for l in infile)
for line, count in counts.most_common():
    if count > 1:
        if not args.simple:
            print str(line) + ": " + str(count)
        total_count += count

if args.simple:
    if total_count > 0:
        print total_count
    elif not args.quiet:
        print total_count
else:
    if total_count == 0 and not args.quiet:
        print "No constraint violations found"

