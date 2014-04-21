#!/usr/bin/env python

import argparse, psycopg2, re, sys, time
from multiprocessing import Process

parser = argparse.ArgumentParser(description="Script for reapplying indexes on child tables in a partition set after they are changed on the parent table. All indexes on all child tables (not including primary key unless specified) will be dropped and recreated for the given set. Commits are done after each index is dropped/created to help prevent long running transactions & locks.", epilog="NOTE: New index names are made based off the child table name & columns used, so their naming may differ from the name given on the parent. This is done to allow the tool to account for long or duplicate index names. If an index name would be duplicated, an incremental counter is added on to the end of the index name to allow it to be created. Use the --dryrun option first to see what it will do and which names may cause dupes to be handled like this.")
parser.add_argument('-p', '--parent', required=True, help="Parent table of an already created partition set. (Required)")
parser.add_argument('-c', '--connection', default="host=localhost", help="""Connection string for use by psycopg to connect to your database. Defaults to "host=localhost".""")
parser.add_argument('--concurrent', action="store_true", help="Create indexes with the CONCURRENTLY option. Note this does not work on primary keys when --primary is given.")
parser.add_argument('--primary', action="store_true", help="By default the primary key is not recreated. Set this option if that is needed. Note this will cause an exclusive lock on the child table.")
parser.add_argument('--drop_concurrent', action="store_true", help="Drop indexes concurrently when recreating them (PostgreSQL >= v9.2). Note this does not work on primary keys when --primary is given.")
parser.add_argument('-j', '--jobs', type=int, default=0, help="Use the python multiprocessing library to recreate indexes in parallel. Value for -j is number of simultaneous jobs to run. Note that this is per table, not per index. Be very careful setting this option if load is a concern on your systems.")
parser.add_argument('-w', '--wait', type=float, default=0, help="Wait the given number of seconds after indexes have finished being created on a table before moving on to the next. When used with -j, this will set the pause between the batches of parallel jobs instead.")
parser.add_argument('--dryrun', action="store_true", help="Show what the script will do without actually running it against the database. Highly recommend reviewing this before running. Note that if multiple indexes would get the same default name, the duplicated name will show in the dryrun (because the index doesn't exist in the catalog to check for it). When the real thing is run, the duplicated names will be handled as stated in NOTE at the end of --help.")
parser.add_argument('-q', '--quiet', action="store_true", help="Turn off all output.")
args = parser.parse_args()

if args.parent.find(".") < 0:
    print("ERROR: Parent table must be schema qualified")
    sys.exit(2)


# Add any checks for version specific features to this function
def check_version(conn, partman_schema):
    cur = conn.cursor()
    if args.drop_concurrent:
        sql = "SELECT " + partman_schema + ".check_version('9.2.0')"
        cur.execute(sql)
        if cur.fetchone()[0] == False:
            print("ERROR: --drop_concurrent option requires PostgreSQL minimum version 9.2.0")
            sys.exit(2)
    cur.close()


def create_conn():
    conn = psycopg2.connect(args.connection)
    return conn


def create_index(conn, partman_schema, child_table, index_list):
    cur = conn.cursor()
    sql = """SELECT schemaname, tablename FROM pg_catalog.pg_tables WHERE schemaname||'.'||tablename = %s"""
    cur.execute(sql, [args.parent])
    result = cur.fetchone()
    parent_schemaname = result[0]
    parent_tablename = result[1]
    cur.execute(sql, [child_table])
    result = cur.fetchone()
    child_tablename = result[1]
    cur.close()
    regex = re.compile(r" ON %s| ON %s" % (args.parent, parent_tablename))
    for i in index_list:
        if i[1] == True and args.primary:
            index_name = child_tablename + "_" + "_".join(i[2].split(","))
            sql = "SELECT " + partman_schema + ".check_name_length('" + index_name + "', p_suffix := '_pk')"
            cur = conn.cursor()
            cur.execute(sql)
            index_name = cur.fetchone()[0]
            cur.close()
            statement = "ALTER TABLE " + child_table + " ADD CONSTRAINT " + index_name + " PRIMARY KEY (" + i[2] + ")"
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
                cur.execute(sql, [parent_schemaname, index_name])
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
                else:
                    break
            cur.close()
            statement = i[0]
            statement = statement.replace(i[3], index_name)  # replace parent index name with child index name
            if args.concurrent:
                statement = statement.replace("CREATE INDEX ", "CREATE INDEX CONCURRENTLY ")
            statement = regex.sub(" ON " + child_table, statement)  
        cur = conn.cursor()
        if not args.quiet:
            print(cur.mogrify(statement))
        if not args.dryrun:
            cur.execute(statement)
        cur.close()


def close_conn(conn):
    conn.close()


def get_children(conn, partman_schema):
    cur = conn.cursor()
    sql = "SELECT " + partman_schema + ".show_partitions(%s, %s)"
    cur.execute(sql, [args.parent, 'ASC'])
    child_list = cur.fetchall()
    cur.close()
    return child_list


def get_drop_list(conn, child_table):
    cur = conn.cursor() 
    sql = """SELECT i.indisprimary, n.nspname||'.'||c.relname, t.conname
            FROM pg_catalog.pg_index i 
            JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
            JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
            LEFT JOIN pg_catalog.pg_constraint t ON c.oid = t.conindid
            WHERE i.indrelid = %s::regclass"""
    cur.execute(sql, [child_table])
    drop_tuple = cur.fetchall()
    cur.close()
    drop_list = []
    for d in drop_tuple:
        if d[0] == True and args.primary:
            statement = "ALTER TABLE " + child_table + " DROP CONSTRAINT " + d[2]
            drop_list.append(statement)
        elif d[0] == False:
            if args.drop_concurrent:
                statement = "DROP INDEX CONCURRENTLY " + d[1]
            else:
                statement = "DROP INDEX " + d[1]
            drop_list.append(statement)
    return drop_list


def get_parent_indexes(conn):
    cur = conn.cursor()
    sql = """SELECT 
            pg_get_indexdef(indexrelid) AS statement
            , i.indisprimary
            , ( SELECT array_to_string(array_agg( a.attname ORDER by x.r ), ',') 
                FROM pg_catalog.pg_attribute a 
                JOIN ( SELECT k, row_number() over () as r 
                        FROM unnest(i.indkey) k ) as x 
                ON a.attnum = x.k AND a.attrelid = i.indrelid
            ) AS indkey_names
            , c.relname
            FROM pg_catalog.pg_index i
            JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
            WHERE i.indrelid = %s::regclass
                AND i.indisvalid"""
    cur.execute(sql, [args.parent])
    parent_index_list = cur.fetchall()
    return parent_index_list


def get_partman_schema(conn):
    cur = conn.cursor()
    sql = "SELECT nspname FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
    cur.execute(sql)
    partman_schema = cur.fetchone()[0]
    cur.close()
    return partman_schema


def reindex_proc(child_table, partman_schema):
    conn = create_conn()
    conn.autocommit = True # must be turned on to support CONCURRENTLY
    cur = conn.cursor()
    drop_list = get_drop_list(conn, child_table)
    parent_index_list = get_parent_indexes(conn)
    for d in drop_list:
        if not args.quiet:
            print(cur.mogrify(d))
        if not args.dryrun: 
            cur.execute(d)

    create_index(conn, partman_schema, child_table, parent_index_list)

    sql = "ANALYZE " + child_table
    if not args.quiet:
        print(cur.mogrify(sql))
    if not args.dryrun:
        cur.execute(sql)
    cur.close()
    close_conn(conn)


if __name__ == "__main__":
    conn = create_conn()
    cur = conn.cursor()
    partman_schema = get_partman_schema(conn)
    check_version(conn, partman_schema)
    child_list = get_children(conn, partman_schema)
    close_conn(conn)

    if args.jobs == 0:
         for c in child_list:
            reindex_proc(c[0], partman_schema)
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
                p = Process(target=reindex_proc, args=(c[0],partman_schema))
                p.start()
                processlist.append(p)
            for j in processlist:
                j.join()
            if args.wait > 0:
                time.sleep(args.wait)

