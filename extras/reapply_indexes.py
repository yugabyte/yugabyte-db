import psycopg2, sys, getopt, time
from multiprocessing import Process 

help_string = """
Script for reapplying indexes on child tables in a partition set after they are changed on the parent table. 
All indexes on all child tables (not including primary key unless specified) will be dropped and recreated for the given set.
Commits are done after each index is dropped/created to help prevent long running transactions & locks.

    --parent (-p):          Parent table of an already created partition set. Required.\n
    --connection (-c):      Connection string for use by psycopg to connect to your database. Defaults to "host=localhost".
                            Highly recommended to use .pgpass file to keep credentials secure.\n
    --concurrent:           Create indexes with the CONCURRENTLY option. Note this does not work on primary keys when --primary is given.\n
    --primary:              By default the primary key is not recreated. Set this option if that is needed.
                            Note this will cause an exclusive lock on the child table.\n
    --jobs (-j):            Use the python multiprocessing library to recreate indexes in parallel. Note that this is per table, not per index.
                            Be very careful setting this option if load is a concern on your systems.\n
    --wait (-w):            Wait the given number of seconds after indexes have finished being created on a table before moving on to the next.
                            When used with -j, this will set the pause between the batches of parallel jobs instead.\n
    --dryrun:               Show what the script will do without actually running it against the database.
                            Highly recommend reviewing this before running.\n
    --quiet:                Turn off all output.

WARNING: The default, postgres generated index name is used for all children when recreating indexes. 
This may cause index naming conflicts if you have multiple, expression indexes that use the same column(s). 
Also, if your child table names are close to the object length limit (63 chars), you may run into naming conflicts 
when the index name truncates the original table name to add _idx or _pkey.
Please DO NOT use this tool to reindex your partition set if either of these cases apply! Use the --dryrun option first.
"""

try:
    opts, args = getopt.getopt(sys.argv[1:], "hqc:p:w:j:", ["help", "quiet", "concurrent", "primary", "dryrun", "connection=", "parent=", "wait=", "jobs="])
except getopt.GetoptError:
    print "Invalid argument"
    print help_string
    sys.exit(2)

arg_parent = ""
arg_connection = "host=localhost"
arg_wait = ""
arg_quiet = ""
arg_concurrent = ""
arg_primary = ""
arg_dryrun = ""
arg_jobs = ""
for opt, arg in opts:
   if opt in ("-h", "--help"):
        print help_string
        sys.exit()
   elif opt in ("-q", "--quiet"):
       arg_quiet = 1 
   elif opt in ("-p", "--parent"):
       arg_parent = arg
   elif opt in ("-c", "--connection"):
       arg_connection = arg
   elif opt in ("--concurrent"):
       arg_concurrent = 1
   elif opt in ("-w", "--wait"):
       arg_wait = float(arg)
   elif opt in ("--primary"):
       arg_primary = 1
   elif opt in ("--dryrun"):
       arg_dryrun = 1
   elif opt in ("-j", "--jobs"):
       arg_jobs = int(arg)

if arg_parent == "":
    print "--parent (-p) argument is required"
    sys.exit(2)
    
if arg_parent.find(".") < 0:
    print "Parent table must be schema qualified"
    sys.exit(2)

def create_conn():
    conn = psycopg2.connect(arg_connection)
    return conn

def close_conn(conn):
    conn.close()

def get_partman_schema(conn):
    cur = conn.cursor()
    sql = "SELECT nspname FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
    cur.execute(sql)
    partman_schema = cur.fetchone()[0]
    cur.close()
    return partman_schema


def get_indexes(conn, child_table):
    cur = conn.cursor()
    sql = """SELECT 
            pg_get_indexdef(indexrelid) AS statement
            , i.indisprimary
            , ( SELECT array_to_string(array_agg( a.attname ORDER by x.r ), ', ') 
                FROM pg_catalog.pg_attribute a 
                JOIN ( SELECT k, row_number() over () as r 
                        FROM unnest(i.indkey) k ) as x 
                ON a.attnum = x.k AND a.attrelid = i.indrelid
                WHERE a.attnotnull
            ) AS indkey_names
            , c.relname
            FROM pg_catalog.pg_index i
            JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
            WHERE i.indrelid = %s::regclass
                AND i.indisvalid"""
    cur.execute(sql, [arg_parent])
    index_tuple = cur.fetchall()
    cur.close()
    index_list = []
    for i in index_tuple:
        if i[1] == True and arg_primary == 1:
            statement = "ALTER TABLE " + child_table + " ADD PRIMARY KEY (" + i[2] + ")"
            index_list.append(statement)
        elif i[1] == False:
            statement = i[0]
            statement = statement.replace(i[3], "")  # remove parent index name
            if arg_concurrent == 1:
                statement = statement.replace("CREATE INDEX ", "CREATE INDEX CONCURRENTLY ")
            statement = statement.replace(" ON " + arg_parent, " ON " + child_table)
            index_list.append(statement)
    return index_list
        

def get_children(conn, partman_schema):
    cur = conn.cursor()
    sql = "SELECT " + partman_schema + ".show_partitions(%s)"
    cur.execute(sql, [arg_parent])
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
        if d[0] == True and arg_primary == 1:
            statement = "ALTER TABLE " + child_table + " DROP CONSTRAINT " + d[2]
            drop_list.append(statement)
        elif d[0] == False:
            statement = "DROP INDEX " + d[1]
            drop_list.append(statement)
    return drop_list
   

def reindex_proc(child_table):
    conn = create_conn()
    conn.autocommit = True # must be turned on to support CONCURRENTLY
    cur = conn.cursor()
    drop_list = get_drop_list(conn, child_table)
    index_list = get_indexes(conn, child_table)
    for d in drop_list:
        if arg_quiet == "":
            print cur.mogrify(d)
        if arg_dryrun == "": 
            cur.execute(d)
    for i in index_list:
        if arg_quiet == "": 
            print cur.mogrify(i)
        if arg_dryrun == "": 
            cur.execute(i)
    sql = "ANALYZE " + child_table
    if arg_quiet == "":
        print cur.mogrify(sql)
    if arg_dryrun == "":
        cur.execute(sql)
    cur.close()
    close_conn(conn)


if __name__ == "__main__":
    conn = create_conn()
    cur = conn.cursor()
    partman_schema = get_partman_schema(conn)
    child_list = get_children(conn, partman_schema)
    close_conn(conn)

    if arg_jobs == "":
         for c in child_list:
            reindex_proc(c[0])
            if arg_wait != "":
                time.sleep(arg_wait)       
    else: 
        child_list.reverse()
        while len(child_list) > 0:
            if arg_quiet == "":
                print "Jobs left in queue: " + str(len(child_list))
            if len(child_list) < arg_jobs:
                arg_jobs = len(child_list)
            processlist = []
            for num in range(0, arg_jobs):
                c = child_list.pop()
                p = Process(target=reindex_proc, args=(c[0],))
                p.start()
            for j in processlist:
                j.join()
            if arg_wait != "":
                time.sleep(arg_wait)

