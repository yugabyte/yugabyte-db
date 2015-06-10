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
        sql = """SELECT keys.confrelid::regclass::text AS ref_table
                    , '"'||string_agg(att.attname, '","')||'"' AS ref_column
                    , '"'||string_agg(att2.attname, '","')||'"' AS child_column
                    , keys.confmatchtype
                    , keys.confupdtype
                    , keys.confdeltype
                    , keys.condeferrable
                    , keys.condeferred
                FROM
                    ( SELECT unnest(con.conkey) as ref
                            , unnest(con.confkey) as child
                            , con.confrelid
                            , con.conrelid
                            , con.condeferred
                            , con.condeferrable
                            , con.confupdtype
                            , con.confdeltype
                            , con.confmatchtype
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
                GROUP BY keys.confrelid, keys.condeferred, keys.condeferrable, keys.confupdtype, keys.confdeltype, keys.confmatchtype""";
        if args.debug:
            print(cur.mogrify(sql, [args.parent]))
        cur.execute(sql, [args.parent])
        parent_fkeys = cur.fetchall()
        for pfk in parent_fkeys:
            alter_sql = "ALTER TABLE " + c[0] + " ADD FOREIGN KEY (" + pfk[2] + ") REFERENCES " + pfk[0] + "(" + pfk[1] + ")"

            if pfk[3] == "f":
                alter_sql += " MATCH FULL "
            elif pfk[3] == "s":
                alter_sql += " MATCH SIMPLE "
            elif pfk[3] == "p":
                alter_sql += " MATCH PARTIAL "

            if pfk[4] == "a":
                alter_sql += " ON UPDATE NO ACTION "
            elif pfk[4] == "r":
                alter_sql += " ON UPDATE RESTRICT "
            elif pfk[4] == "c":
                alter_sql += " ON UPDATE CASCADE "
            elif pfk[4] == "n":
                alter_sql += " ON UPDATE SET NULL "
            elif pfk[4] == "d":
                alter_sql += " ON UPDATE SET DEFAULT "

            if pfk[5] == "a":
                alter_sql += " ON DELETE NO ACTION "
            elif pfk[5] == "r":
                alter_sql += " ON DELETE RESTRICT "
            elif pfk[5] == "c":
                alter_sql += " ON DELETE CASCADE "
            elif pfk[5] == "n":
                alter_sql += " ON DELETE SET NULL "
            elif pfk[5] == "d":
                alter_sql += " ON DELETE SET DEFAULT "

            if pfk[6] == True and pfk[7] == True:
                alter_sql += " DEFERRABLE INITIALLY DEFERRED "
            elif pfk[6] == False and pfk[7] == False:
                alter_sql += " NOT DEFERRABLE "
            elif pfk[6] == True and pfk[7] == False:
                alter_sql += " DEFERRABLE INITIALLY IMMEDIATE "

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


def get_child_tables(conn):
    if not args.quiet:
        print("Getting list of child tables...")
    cur = conn.cursor()
    sql = """SELECT n.nspname::text ||'.'|| c.relname::text AS partition_name FROM
    pg_catalog.pg_inherits h
    JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE h.inhparent = %s::regclass"""

    if args.debug:
        print(cur.mogrify(sql, [args.parent]))
    cur.execute(sql, [args.parent])
    result = cur.fetchall()
    return result


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

    child_tables = get_child_tables(conn)

    drop_foreign_keys(conn, child_tables)
    apply_foreign_keys(conn, child_tables)

    if not args.quiet:
        print("Done!")
    close_conn(conn)
