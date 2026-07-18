#!/usr/bin/env python

import argparse, psycopg2, sys

partman_version = "3.1.0"

parser = argparse.ArgumentParser(description="This script will reapply the foreign keys on a parent table to all child tables in an inheritance set. Any existing foreign keys on child tables will be dropped in order to match the parent. A commit is done after each foreign key application to avoid excessive contention. Note that this script can work on any inheritance set, not just partition sets managed by pg_partman.")
parser.add_argument('-p','--parent', help="Parent table of an already created partition set. (Required)")
parser.add_argument('-c','--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket).""")
parser.add_argument('-q', '--quiet', action="store_true", help="Switch setting to stop all output during and after partitioning undo.")
parser.add_argument('--dryrun', action="store_true", help="Show what the script will do without actually running it against the database. Highly recommend reviewing this before running.")
parser.add_argument('--nonpartman', action="store_true", help="If the partition set you are running this on is not managed by pg_partman, set this flag. Otherwise internal pg_partman functions are used and this script may not work. When this is set the order that the tables are rekeyed is alphabetical instead of logical.")
parser.add_argument('--debug', action="store_true", help="Show additional debugging output")
parser.add_argument('--version', action="store_true", help="Print out the minimum version of pg_partman this script is meant to work with. The version of pg_partman installed may be greater than this.")
args = parser.parse_args()


def apply_foreign_keys(conn, child_tables, partman_schema):
    if not args.quiet:
        print("Applying foreign keys to child tables...")
    cur = conn.cursor()

    # Get template table if exists to use its indexes
    sql = "SELECT template_table FROM " + partman_schema + ".part_config WHERE parent_table = %s AND partition_type = 'native'"
    cur.execute(sql, [args.parent])
    template_table = cur.fetchone()

    for c in child_tables:
        sql = """SELECT pg_get_constraintdef(con.oid) AS constraint_def
                    FROM pg_catalog.pg_constraint con
                    JOIN pg_catalog.pg_class c ON con.conrelid = c.oid
                    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
                    WHERE n.nspname ||'.'|| c.relname = %s
                    AND contype = 'f'""";

        if template_table == None:
            if args.debug:
                print(cur.mogrify(sql, [args.parent]))
            cur.execute(sql, [args.parent])
        else:
            if args.debug:
                print(cur.mogrify(sql, [template_table]))
            cur.execute(sql, [template_table])

        parent_fkeys = cur.fetchall()
        for pfk in parent_fkeys:
            alter_sql = "ALTER TABLE \"" + c[0] + "\".\"" + c[1] + "\" ADD " + pfk[0]

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
            WHERE table_schema = %s AND table_name = %s AND constraint_type = 'FOREIGN KEY'"""
        if args.debug:
            print(cur.mogrify(sql, [ c[0], c[1] ]))
        cur.execute(sql, [ c[0], c[1] ])
        child_fkeys = cur.fetchall()
        for cfk in child_fkeys:
            alter_sql = "ALTER TABLE \"" + c[0] + "\".\"" + c[1] + "\" DROP CONSTRAINT \"" + cfk[0] + "\""
            if not args.quiet:
                print(alter_sql)
            if not args.dryrun:
                cur.execute(alter_sql)


def get_child_tables(conn, partman_schema):
    if not args.quiet:
        print("Getting list of child tables...")
    cur = conn.cursor()
    if args.nonpartman == False:
        sql = "SELECT partition_schemaname, partition_tablename FROM " + partman_schema + ".show_partitions(%s)"
    else:
        sql = """
            WITH parent_info AS (
                SELECT c1.oid FROM pg_catalog.pg_class c1
                JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
                WHERE n1.nspname ||'.'|| c1.relname = %s
            )
            SELECT n.nspname::text, c.relname::text AS partition_name FROM
            pg_catalog.pg_inherits h
            JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
            JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
            JOIN parent_info pi ON h.inhparent = pi.oid
            ORDER BY 1,2"""
    if args.debug:
        print(cur.mogrify(sql, [args.parent]))
    cur.execute(sql, [args.parent])
    result = cur.fetchall()
    return result


def get_partman_schema(conn):
    cur = conn.cursor()
    sql = "SELECT nspname FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
    cur.execute(sql)
    partman_schema = "\"" + cur.fetchone()[0] + "\""
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
    apply_foreign_keys(conn, child_tables, partman_schema)

    if not args.quiet:
        print("Done!")
    close_conn(conn)
