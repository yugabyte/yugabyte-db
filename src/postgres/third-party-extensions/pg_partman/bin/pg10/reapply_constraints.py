#!/usr/bin/env python

import argparse, psycopg2, sys, time
from multiprocessing import Process

partman_version = "2.2.0"

parser = argparse.ArgumentParser(description="Script for reapplying additional constraints managed by pg_partman on child tables. See docs for additional info on this special constraint management. Script runs in two distinct modes: 1) Drop all constraints  2) Apply all constraints. Typical usage would be to run the drop mode, edit the data, then run apply mode to re-create all constraints on a partition set.")
parser.add_argument('-p', '--parent', help="Parent table of an already created partition set. (Required)")
parser.add_argument('-c','--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket).""")
parser.add_argument('-d', '--drop_constraints', action="store_true", help="Drop all constraints managed by pg_partman. Drops constraints on all child tables including current & future.")
parser.add_argument('-a', '--add_constraints', action="store_true", help="Apply configured constraints to all child tables older than the optimize_constraint value.")
parser.add_argument('-j', '--jobs', type=int, default=0, help="Use the python multiprocessing library to recreate indexes in parallel. Value for -j is number of simultaneous jobs to run. Note that this is per table, not per index. Be very careful setting this option if load is a concern on your systems.")
parser.add_argument('-w', '--wait', type=float, default=0, help="Wait the given number of seconds after a table has had its constraints dropped or applied before moving on to the next. When used with -j, this will set the pause between the batches of parallel jobs instead.")
parser.add_argument('--dryrun', action="store_true", help="Show what the script will do without actually running it against the database. Highly recommend reviewing this before running.")
parser.add_argument('-q', '--quiet', action="store_true", help="Turn off all output.")
parser.add_argument('--version', action="store_true", help="Print out the minimum version of pg_partman this script is meant to work with. The version of pg_partman installed may be greater than this.")
args = parser.parse_args()


def apply_proc(child_table, partman_schema):
    conn = create_conn()
    conn.autocommit = True
    cur = conn.cursor()
    sql = "SELECT " + partman_schema + ".apply_constraints(%s, %s, %s, %s, %s)"
    debug = False;
    if not args.quiet:
        debug = True
        print(cur.mogrify(sql, [args.parent, child_table, False, None, debug]))
    if not args.dryrun:
        cur.execute(sql, [args.parent, child_table, False, None, debug])
    cur.close()
    close_conn(conn)


def create_conn():
    conn = psycopg2.connect(args.connection)
    return conn


def close_conn(conn):
    conn.close()


def drop_proc(child_table, partman_schema):
    conn = create_conn()
    conn.autocommit = True
    cur = conn.cursor()
    sql = "SELECT " + partman_schema + ".drop_constraints(%s, %s, %s)"
    debug = False;
    if not args.quiet:
        debug = True
        print(cur.mogrify(sql, [args.parent, child_table, debug]))
    if not args.dryrun:
        cur.execute(sql, [args.parent, child_table, debug])
    cur.close()
    close_conn(conn)


def get_children(conn, partman_schema):
    cur = conn.cursor()
    sql = "SELECT partition_schemaname||'.'||partition_tablename FROM " + partman_schema + ".show_partitions(%s, %s)"
    cur.execute(sql, [args.parent, 'ASC'])
    child_list = cur.fetchall()
    cur.close()
    return child_list


def get_partman_schema(conn):
    cur = conn.cursor()
    sql = "SELECT nspname FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
    cur.execute(sql)
    partman_schema = "\"" + cur.fetchone()[0] + "\""
    cur.close()
    return partman_schema


def get_config_values(conn, partman_schema):
    # [0] = premake, [1] = optimize_constraint
    cur = conn.cursor()
    sql = "SELECT premake, optimize_constraint FROM " + partman_schema + ".part_config WHERE parent_table = %s"
    cur.execute(sql, [args.parent])
    config_values = cur.fetchone()
    cur.close()
    return config_values 


def get_quoted_parent_table(conn):
    cur = conn.cursor()
    sql = "SELECT schemaname, tablename FROM pg_catalog.pg_tables WHERE schemaname||'.'||tablename = %s"
    cur.execute(sql, [args.parent])
    result = cur.fetchone()
    if result == None:
        print("Given parent table ("+args.parent+") does not exist")
        sys.exit(2)
    quoted_parent_table = "\"" + result[0] + "\".\"" + result[1] + "\""
    cur.close()
    return quoted_parent_table


def print_version():
    print(partman_version)
    sys.exit()


if __name__ == "__main__":

    if args.version:
        print_version()

    if args.parent == None:
        print("-p/--parent option is required")
        sys.exit(2)

    if args.parent.find(".") < 0:
        print("Parent table must be schema qualified")
        sys.exit(2)

    if args.drop_constraints and args.add_constraints: 
        print("Can only set one or the other of --drop_constraints (-d) and --add_constraints (-a)")
        sys.exit(2)

    if (args.drop_constraints == False) and (args.add_constraints == False):
        print("Must set one of --drop_constraints (-d) or --add_constraints (-a)")
        sys.exit(2)

    main_conn = create_conn()
    partman_schema = get_partman_schema(main_conn)
    quoted_parent_table = get_quoted_parent_table(main_conn)
    child_list = get_children(main_conn, partman_schema)
    config_values = get_config_values(main_conn, partman_schema)
    premake = int(config_values[0])
    optimize_constraint = int(config_values[1])
    if args.add_constraints:
        # Remove tables from the list of child tables that shouldn't have constraints yet 
        for x in range(optimize_constraint + premake + 1):
            child_list.pop()

    if args.jobs == 0:
        for c in child_list:
            if args.drop_constraints:
               drop_proc(c[0], partman_schema)
            if args.add_constraints:
               apply_proc(c[0], partman_schema)
            if args.wait > 0:
                time.sleep(args.wait)
    else:
        child_list.reverse()
        while len(child_list) > 0:
            if not args.quiet:
                print("Jobs left in queue: " + str(len(child_list)))
            if len(child_list) < args.jobs:
                args.jobs = len(child_list)
            processlist = []
            for num in range(0, args.jobs):
                c = child_list.pop()
                if args.drop_constraints:
                    p = Process(target=drop_proc, args=(c[0], partman_schema))
                if args.add_constraints:
                    p = Process(target=apply_proc, args=(c[0], partman_schema))
                p.start()
                processlist.append(p)
            for j in processlist:
                j.join()
            if args.wait > 0:
                time.sleep(args.wait)

    sql = 'ANALYZE ' + quoted_parent_table
    main_cur = main_conn.cursor()
    if not args.quiet:
        print(main_cur.mogrify(sql))
    if not args.dryrun:
        main_cur.execute(sql)

    close_conn(main_conn)

