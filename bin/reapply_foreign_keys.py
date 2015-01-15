#!/usr/bin/env python

import argparse, psycopg2, sys, time

partman_version = "1.8.0"

parser = argparse.ArgumentParser(description="This script will reapply the foreign keys on a parent table to all child tables in an inheritance set. Any existing foreign keys on child tables will be dropped in order to match the parent. A commit is done after each foreign key application to avoid excessive contention. Note that this script can work on any inheritance set, not just partition sets managed by pg_partman.")
parser.add_argument('-p','--parent', help="Parent table of an already created partition set. (Required)")
parser.add_argument('-c','--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket).""")
parser.add_argument('-q', '--quiet', action="store_true", help="Switch setting to stop all output during and after partitioning undo.")
parser.add_argument('--dryrun', action="store_true", help="Show what the script will do without actually running it against the database. Highly recommend reviewing this before running.")
parser.add_argument('--version', action="store_true", help="Print out the minimum version of pg_partman this script is meant to work with. The version of pg_partman installed may be greater than this.")
parser.add_argument('--debug', action="store_true", help="Show additional debugging output")
args = parser.parse_args() 


def apply_foreign_keys(conn, child_tables):
    if not args.quiet:
        print("Applying foreign keys to child tables...")
    cur = conn.cursor()
    for c in child_tables:
        sql = """SELECT keys.conname
                    , keys.confrelid::regclass::text AS ref_table
                    , '"'||string_agg(att.attname, '","')||'"' AS ref_column
                    , '"'||string_agg(att2.attname, '","')||'"' AS child_column
                FROM
                    ( SELECT con.conname
                            , unnest(con.conkey) as ref
                            , unnest(con.confkey) as child
                            , con.confrelid
                            , con.conrelid
                      FROM pg_catalog.pg_class c
                      JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
                      JOIN pg_catalog.pg_constraint con ON c.oid = con.conrelid
                      WHERE n.nspname ||'.'|| c.relname = %s
                      AND con.contype = 'f'
                      ORDER BY con.conkey
                ) keys
                JOIN pg_catalog.pg_class cl ON cl.oid = keys.confrelid
                JOIN pg_catalog.pg_attribute att ON att.attrelid = keys.confrelid AND att.attnum = keys.child
                JOIN pg_catalog.pg_attribute att2 ON att2.attrelid = keys.conrelid AND att2.attnum = keys.ref
                GROUP BY keys.conname, keys.confrelid""";
        if args.debug:
            print(cur.mogrify(sql, [args.parent]))
        cur.execute(sql, [args.parent])
        parent_fkeys = cur.fetchall()
        for pfk in parent_fkeys:
            alter_sql = "ALTER TABLE " + c[0] + " ADD FOREIGN KEY (" + pfk[3] + ") REFERENCES " + pfk[1] + "(" + pfk[2] + ")"
            if not args.quiet:
                print(alter_sql)
            if not args.dryrun:
                cur.execute(alter_sql)


def create_conn():
    conn = psycopg2.connect(args.connection)
    conn.autocommit = True
    return conn


def close_conn(conn):
    conn.close()


def drop_foreign_keys(conn, child_tables):
    if not args.quiet:
        print("Dropping current foreign keys on child tables...")
    cur = conn.cursor()
    for c in child_tables:
        sql = """SELECT constraint_name
            FROM information_schema.table_constraints 
            WHERE table_schema||'.'||table_name = %s AND constraint_type = 'FOREIGN KEY'"""
        if args.debug:
            print(cur.mogrify(sql, [ c[0] ]))
        cur.execute(sql, [ c[0] ])
        child_fkeys = cur.fetchall()
        for cfk in child_fkeys:
            alter_sql = "ALTER TABLE " + c[0] + " DROP CONSTRAINT " + cfk[0]
            if not args.quiet:
                print(alter_sql)
            if not args.dryrun:
                cur.execute(alter_sql)


def get_child_tables(conn, part_schema):
    if not args.quiet:
        print("Getting list of child tables...")
    cur = conn.cursor()
    sql = "SELECT * FROM " + partman_schema + ".show_partitions(%s)"
    if args.debug:
        print(cur.mogrify(sql, [args.parent]))
    cur.execute(sql, [args.parent])
    result = cur.fetchall()
    return result


def get_partman_schema(conn):
    cur = conn.cursor()
    sql = "SELECT nspname FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
    cur.execute(sql)
    partman_schema = cur.fetchone()[0]
    cur.close()
    return partman_schema


def print_version():
    print(partman_version)
    sys.exit()


if __name__ == "__main__":

    if args.version:
        print_version()

    if args.parent == None:
        print("-p/--parent option is required")
        sys.exit(2)

    conn = create_conn()

    partman_schema = get_partman_schema(conn)
    child_tables = get_child_tables(conn, partman_schema)

    drop_foreign_keys(conn, child_tables)
    apply_foreign_keys(conn, child_tables)

    if not args.quiet:
        print("Done!")
    close_conn(conn)
