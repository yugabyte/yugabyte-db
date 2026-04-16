#!/usr/bin/env python

import argparse, psycopg2, re, sys, time
from multiprocessing import Process

partman_version = "4.1.0"

parser = argparse.ArgumentParser(description="Script for reapplying indexes on child tables in a partition set to match the parent table. Any indexes that currently exist on the children and match the definition on the parent will be left as is. There is an option to recreate matching as well indexes if desired, as well as the primary key. Indexes that do not exist on the parent will be dropped. Commits are done after each index is dropped/created to help prevent long running transactions & locks.", epilog="NOTE: New index names are made based off the child table name & columns used, so their naming may differ from the name given on the parent. This is done to allow the tool to account for long or duplicate index names. If an index name would be duplicated, an incremental counter is added on to the end of the index name to allow it to be created. Use the --dryrun option first to see what it will do and which names may cause dupes to be handled like this.")
parser.add_argument('-p', '--parent', help="Parent table of an already created partition set. (Required)")
parser.add_argument('-c','--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket).""")
parser.add_argument('--concurrent', action="store_true", help="Create indexes with the CONCURRENTLY option. Note this does not work on primary keys when --primary is given.")
parser.add_argument('--drop_concurrent', action="store_true", help="If an index is dropped (because it doesn't exist on the parent or because you set them to be recreated), do it concurrently. Note this does not work on primary keys when --primary is given.")
parser.add_argument('-R', '--recreate_all', action="store_true", help="By default, if an index exists on a child and matches the parent, it will not be touched. Setting this option will force all child indexes to be dropped & recreated. Will obey the --concurrent & --drop_concurrent options if given. Will not recreate primary keys unless --primary option is also given.")
parser.add_argument('--primary', action="store_true", help="By default the primary key is not recreated. Set this option if that is needed. Note this will cause an exclusive lock on the child table.")
parser.add_argument('-j', '--jobs', type=int, default=0, help="Use the python multiprocessing library to recreate indexes in parallel. Value for -j is number of simultaneous jobs to run. Note that this is per table, not per index. Be very careful setting this option if load is a concern on your systems.")
parser.add_argument('-w', '--wait', type=float, default=0, help="Wait the given number of seconds after indexes have finished being created on a table before moving on to the next. When used with -j, this will set the pause between the batches of parallel jobs instead.")
parser.add_argument('--dryrun', action="store_true", help="Show what the script will do without actually running it against the database. Highly recommend reviewing this before running. Note that if multiple indexes would get the same default name, the duplicated name will show in the dryrun (because the index doesn't exist in the catalog to check for it). When the real thing is run, the duplicated names will be handled as stated in NOTE at the end of --help.")
parser.add_argument('-q', '--quiet', action="store_true", help="Turn off all output.")
parser.add_argument('--nonpartman', action="store_true", help="If the partition set you are running this on is not managed by pg_partman, set this flag otherwise this script may not work. Note that the pg_partman extension is still required to be installed for this to work since it uses certain internal functions. When this is set the order that the tables are reindexed is alphabetical instead of logical.")
parser.add_argument('--version', action="store_true", help="Print out the minimum version of pg_partman this script is meant to work with. The version of pg_partman installed may be greater than this.")
args = parser.parse_args()

def check_compatibility(conn, partman_schema):
    cur = conn.cursor()
    
    sql = """SELECT current_setting('server_version_num')::int"""
    cur.execute(sql)
    pg_version = int(cur.fetchone()[0])

    if pg_version < 90400:
        print("ERROR: This script requires PostgreSQL minimum version of 9.4.0")
        sys.exit(2)

    sql = "SELECT partition_type FROM " + partman_schema + ".part_config WHERE parent_table = %s"
    cur.execute(sql, [args.parent])
    partition_type = cur.fetchone()[0]

    if pg_version >= 110000 and partition_type == "native":
        print("This script cannot currently work with native partition sets in PG11+. Please use native index inheritance methods if possible.")
        cur.close()
        close_conn(conn)
        sys.exit(2)

    cur.close()


def create_conn():
    conn = psycopg2.connect(args.connection)
    return conn


def create_index(conn, partman_schema, child_schemaname, child_tablename, child_index_list, parent, parent_index_list):
    cur = conn.cursor()
    sql = """SELECT schemaname, tablename FROM pg_catalog.pg_tables WHERE schemaname||'.'||tablename = %s"""
    cur.execute(sql, [parent])
    result = cur.fetchone()
    parent_schemaname = result[0]
    parent_tablename = result[1]
    cur.close()
    parent_match_regex = re.compile(r" ON \"?%s\"?\.\"?%s\"? | ON \"?%s\"?" % (parent_schemaname, parent_tablename, parent_tablename))
    index_match_regex = re.compile(r'(?P<index_def>USING .*)')
    for i in parent_index_list:
        # if there is already a child index that matches the parent index don't try to create it unless --recreate_all set
        if args.recreate_all != True:
            child_found = False
            statement = None
            parinddef = index_match_regex.search(i[0])
            for c in child_index_list:
                chinddef = index_match_regex.search(c[0])
                if chinddef.group('index_def') == parinddef.group('index_def'):
                    child_found = True
                    break
            if child_found:
                continue

        if i[1] == True and args.primary:
            index_name = child_tablename + "_" + "_".join(i[2].split(","))
            sql = "SELECT " + partman_schema + ".check_name_length('" + index_name + "', p_suffix := '_pk')"
            cur = conn.cursor()
            cur.execute(sql)
            index_name = cur.fetchone()[0]
            cur.close()
            quoted_column_names = "\"" + "\",\"".join(i[2].split(",")) + "\""
            statement = "ALTER TABLE \"" + child_schemaname + "\".\"" + child_tablename + "\" ADD CONSTRAINT \"" + index_name + "\" PRIMARY KEY (" + quoted_column_names + ")"
        elif i[1] == False:
            index_name = child_tablename
            if i[2] != None:
                index_name += "_"
                index_name += "_".join(i[2].split(","))
            sql = "SELECT " + partman_schema + ".check_name_length('" + index_name + "', p_suffix := '_idx')"
            cur = conn.cursor()
            cur.execute(sql)
            index_name = cur.fetchone()[0]
            name_counter = 1
            while True:
                sql = "SELECT count(*) FROM pg_class c JOIN pg_namespace n ON c.relnamespace = n.oid WHERE n.nspname = %s AND c.relname = %s"
                cur = conn.cursor()
                cur.execute(sql, [child_schemaname, index_name])
                index_exists = cur.fetchone()[0]
                if index_exists != None and index_exists > 0:
                    index_name = child_tablename
                    if i[2] != None:
                        index_name += "_"
                        index_name += "_".join(i[2].split(","))
                    suffix = "_idx" + str(name_counter)
                    sql = "SELECT " + partman_schema + ".check_name_length('" + index_name + "', p_suffix := '" + suffix + "')"
                    cur = conn.cursor()
                    cur.execute(sql)
                    index_name = cur.fetchone()[0]
                    name_counter += 1
                else:
                    break
            cur.close()
            statement = i[0]
            statement = statement.replace(i[3], index_name)  # replace parent index name with child index name
            if args.concurrent:
                statement = statement.replace("CREATE INDEX ", "CREATE INDEX CONCURRENTLY ")
            statement = parent_match_regex.sub(" ON \"" + child_schemaname + "\".\"" + child_tablename + "\" ", statement)
        cur = conn.cursor()
        if statement != None:
            if not args.quiet:
                print(cur.mogrify(statement))
            if not args.dryrun:
                cur.execute(statement)
        cur.close()


def close_conn(conn):
    conn.close()


def drop_index(conn, child_schemaname, child_tablename, child_index_list, parent_index_list):
    cur = conn.cursor()
    for d in child_index_list:
        if d[1] == True and args.primary:
            statement = "ALTER TABLE \"" + child_schemaname + "\".\"" + child_tablename + "\" DROP CONSTRAINT \"" + d[4] + "\""
            if not args.quiet:
                print(cur.mogrify(statement))
            if not args.dryrun:
                cur.execute(statement)
        elif d[1] == False:
            if args.drop_concurrent:
                statement = "DROP INDEX CONCURRENTLY \"" + d[2] + "\".\"" + d[3] + "\""
            else:
                statement = "DROP INDEX \"" + d[2] + "\".\"" + d[3] + "\""

            if args.recreate_all != True:
                pat = re.compile(r'(?P<index_def>USING .*)')
                # if there is a parent index that matches the child index
                # don't try to drop it - we'd just have to recreate it
                chinddef = pat.search(d[0])
                parent_found = False
                for p in parent_index_list:
                    parent_found = False
                    parinddef = pat.search(p[0])
                    if chinddef.group('index_def') == parinddef.group('index_def'):
                        parent_found = True
                        break
                if not parent_found:
                    if not args.quiet:
                        print(cur.mogrify(statement))
                    if not args.dryrun:
                        cur.execute(statement)
            else:
                if not args.quiet:
                    print(cur.mogrify(statement))
                if not args.dryrun:
                    cur.execute(statement)


def get_children(conn, partman_schema):
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
    cur.execute(sql, [args.parent])
    child_list = cur.fetchall()
    cur.close()
    return child_list


def get_child_index_list(conn, child_schemaname, child_tablename):
    cur = conn.cursor()
    sql = """
            WITH child_info AS (
                SELECT c1.oid FROM pg_catalog.pg_class c1
                JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
                WHERE n1.nspname = %s
                AND c1.relname = %s
            )
            SELECT pg_get_indexdef(indexrelid) AS statement
            , i.indisprimary
            , n.nspname AS index_schemaname
            , c.relname AS index_name
            , t.conname
            FROM pg_catalog.pg_index i
            JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
            JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
            LEFT JOIN pg_catalog.pg_constraint t ON c.oid = t.conindid
            JOIN child_info ci ON i.indrelid = ci.oid """
    cur.execute(sql, [child_schemaname, child_tablename])
    child_index_list = cur.fetchall()
    cur.close()
    return child_index_list


def get_parent_index_list(conn, parent):
    cur = conn.cursor()

    sql = """
            WITH parent_info AS (
                SELECT c1.oid FROM pg_catalog.pg_class c1
                JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
                WHERE n1.nspname||'.'||c1.relname = %s
            )
            SELECT
            pg_get_indexdef(indexrelid) AS statement
            , i.indisprimary
            , ( SELECT array_to_string(array_agg( a.attname ORDER by x.r ), ',')
                FROM pg_catalog.pg_attribute a
                JOIN ( SELECT k, row_number() over () as r
                        FROM unnest(i.indkey) k ) as x
                ON a.attnum = x.k AND a.attrelid = i.indrelid
            ) AS indkey_names
            , c.relname AS index_name
            FROM pg_catalog.pg_index i
            JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
            JOIN parent_info pi ON i.indrelid = pi.oid
            WHERE i.indisvalid
            ORDER BY 1"""
    cur.execute(sql, [parent])
    parent_index_list = cur.fetchall()
    return parent_index_list


def get_parent(conn, partman_schema):
    cur = conn.cursor()

    sql = "SELECT template_table FROM " + partman_schema + ".part_config WHERE parent_table = %s AND partition_type = 'native'"
    cur.execute(sql, [args.parent])
    template_table = cur.fetchone()

    if template_table is None:
        return args.parent
    else:
        return template_table[0]


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


def reindex_proc(child_schemaname, child_tablename, parent, parent_index_list, partman_schema):
    conn = create_conn()
    conn.autocommit = True # must be turned on to support CONCURRENTLY
    cur = conn.cursor()
    child_index_list = get_child_index_list(conn, child_schemaname, child_tablename)
    drop_index(conn, child_schemaname, child_tablename, child_index_list, parent_index_list)
    create_index(conn, partman_schema, child_schemaname, child_tablename, child_index_list, parent, parent_index_list)

    sql = "ANALYZE \"" + child_schemaname + "\".\"" + child_tablename + "\""
    if not args.quiet:
        print(cur.mogrify(sql))
    if not args.dryrun:
        cur.execute(sql)
    cur.close()
    close_conn(conn)


if __name__ == "__main__":

    if args.version:
        print_version()

    if args.parent == None:
        print("-p/--parent option is required")
        sys.exit(2)

    if args.parent.find(".") < 0:
        print("ERROR: Parent table must be schema qualified")
        sys.exit(2)

    conn = create_conn()

    partman_schema = get_partman_schema(conn)
    check_compatibility(conn,partman_schema)
    parent = get_parent(conn, partman_schema)
    parent_index_list = get_parent_index_list(conn, parent)
    child_list = get_children(conn, partman_schema)
    close_conn(conn)

    if args.jobs == 0:
         for c in child_list:
            reindex_proc(c[0], c[1], parent, parent_index_list, partman_schema)
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
                p = Process(target=reindex_proc, args=(c[0], c[1], parent, parent_index_list, partman_schema))
                p.start()
                processlist.append(p)
            for j in processlist:
                j.join()
            if args.wait > 0:
                time.sleep(args.wait)
