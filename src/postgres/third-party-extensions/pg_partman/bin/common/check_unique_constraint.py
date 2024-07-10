#!/usr/bin/env python

import argparse, collections, psycopg2, sys, tempfile

partman_version = "2.0.0"

parser = argparse.ArgumentParser(description="This script is used to check that all rows in a partition set are unique for the given columns. Since unique constraints are not applied across partition sets, this cannot be enforced within the database. This script can be used as a monitor to ensure uniquness. If any unique violations are found, the values, along with a count of each, are output.")
parser.add_argument('-p', '--parent',  help="Parent table of the partition set to be checked")
parser.add_argument('-l', '--column_list', help="Comma separated list of columns that make up the unique constraint to be checked")
parser.add_argument('-c','--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket).""")
parser.add_argument('-t', '--temp', help="Path to a writable folder that can be used for temp working files. Defaults system temp folder.")
parser.add_argument('--psql', help="Full path to psql binary if not in current PATH")
parser.add_argument('--simple', action="store_true", help="Output a single integer value with the total duplicate count. Use this for monitoring software that requires a simple value to be checked for.")
parser.add_argument('--index_scan', action="store_true", help="By default index scans are disabled to force the script to check the actual table data with sequential scans. Set this option if you want the script to allow index scans to be used (does not guarentee that they will be used).")
parser.add_argument('-q', '--quiet', action="store_true", help="Suppress all output unless there is a constraint violation found.")
parser.add_argument('--version', action="store_true", help="Print out the minimum version of pg_partman this script is meant to work with. The version of pg_partman installed may be greater than this.")
args = parser.parse_args()

if args.version:
    print(partman_version)
    sys.exit()

if args.parent == None:
    print("-p/--parent option is required")
    sys.exit(2)

if args.column_list == None:
    print("-l/--column_list option is required")
    sys.exit(2)

if args.temp == None:
    tmp_copy_file = tempfile.NamedTemporaryFile(prefix="partman_constraint")
else:
    tmp_copy_file = tempfile.NamedTemporaryFile(prefix="partman_constraint", dir=args.temp)

fh = open(tmp_copy_file.name, 'w')
conn = psycopg2.connect(args.connection)
conn.set_session(isolation_level="REPEATABLE READ", readonly=True)
cur = conn.cursor()

# Get separate object names so they can be properly quoted
sql = "SELECT schemaname, tablename FROM pg_catalog.pg_tables WHERE schemaname||'.'||tablename = %s"
cur.execute(sql, [args.parent])
result = cur.fetchone()
if result == None:
    print("Given parent table ("+args.parent+") does not exist")
    sys.exit(2)
quoted_parent_table = "\"" + result[0] + "\".\"" + result[1] + "\""
#print(quoted_parent_table)

# Recreated column list with double quotes around each
cols_array = args.column_list.split(",")
quoted_col_list = "\"" + "\",\"".join(cols_array) + "\""
#print(quoted_col_list)

if args.index_scan == False:
    sql = """set enable_bitmapscan = false;
    set enable_indexonlyscan = false;
    set enable_indexscan = false;
    set enable_seqscan = true;"""
else:
    sql = """set enable_bitmapscan = true;
    set enable_indexonlyscan = true;
    set enable_indexscan = true;
    set enable_seqscan = false;"""
cur.execute(sql)
cur.close()
cur = conn.cursor()
if not args.quiet:
    print("Dumping out column data to temp file...")
copy_statement = "COPY (SELECT " + quoted_col_list + " FROM " + quoted_parent_table + ") TO STDOUT WITH DELIMITER ','"
#print(copy_statement)
cur.copy_expert(copy_statement, fh)
conn.rollback()
conn.close()
fh.close()

total_count = 0
if not args.quiet:
    print("Checking for dupes...")
with open(tmp_copy_file.name) as infile:
    counts = collections.Counter(l.strip() for l in infile)
for line, count in counts.most_common():
    if count > 1:
        if not args.simple:
            print(str(line) + ": " + str(count))
        total_count += count

if args.simple:
    if total_count > 0:
        print(total_count)
    elif not args.quiet:
        print(total_count)
else:
    if total_count == 0 and not args.quiet:
        print("No constraint violations found")
