import sys, getopt, hashlib, subprocess, psycopg2, os, os.path

help_string = """
This script will dump out and then drop all tables contained in the designated schema using pg_dump. 
Each table will be in its own separate file along with a SHA-512 hash of the dump file. 
Tables are not dropped from the database if pg_dump does not return successfully.
All dump_* option defaults are the same as they would be for pg_dump if they are not given.

    --schema (-n):          The schema that contains the tables that will be dumped. (Required)\n
    --connection (-c):      Connection string for use by psycopg. Must be able to select pg_catalog.pg_tables 
                            in the relevant database and drop all tables in the given schema. 
                            Defaults to "host=localhost".
                            Note this is distinct from the parameters sent to pg_dump. \n
    --output (-o):          Path to dump file output location. Default is where the script is run from.\n
    --dump_database (-d):   Used for pg_dump, same as its final database name parameter.\n
    --dump_host (-h):       Used for pg_dump, same as its --host option.\n
    --dump_username (-U):   Used for pg_dump, same as its --username option.\n
    --dump_port (-p):       Used for pg_dump, same as its --port option.\n
    --pg_dump_path:         Path to pg_dump binary location. Must set if not in current PATH.\n
    --Fp:                   Dump using pg_dump plain text format. Default is binary custom (-Fc).\n
    --nohashfile:           Do NOT create a separate file with the SHA-512 hash of the dump.\n
                            If dump files are very large, hash generation can possibly take a long time.\n
    --nodrop:               Do NOT drop the tables from the given schema after dumping/hashing.\n
    --verbose (-v):         Provide more verbose output.\n

Note: The connection options for psyocpg and pg_dump were separated out due to distinct differences in 
their requirements depending on your database connection configuration. 
"""

try:
    opts, args = getopt.getopt(sys.argv[1:], "?vn:c:h:U:p:d:o:", ["help","Fp","nohashfile","nodrop","verbose","connection=","schema=","dump_host=","dump_username=","dump_port=","dump_database=","output=","pg_dump_path="])
except getopt.GetoptError:
    print "Invalid argument"
    print help_string
    sys.exit(2)

arg_schema = ""
arg_connection = "host=localhost"
arg_host = ""
arg_username = ""
arg_port = ""
arg_database = ""
arg_output = os.getcwd() 
arg_pgdump = ""
arg_fp = ""
arg_nohashfile = ""
arg_nodrop = ""
arg_verbose = ""
for opt, arg in opts:
    if opt in ("-?", "--help"):
        print help_string
        sys.exit()
    elif opt in ("-n", "--schema"):
        arg_schema = arg
    elif opt in ("-c", "--connection"):
        arg_connection = arg
    elif opt in ("-d", "--dump_database"):
        arg_database = arg
    elif opt in ("-h", "--dump_host"):
        arg_host = arg
    elif opt in ("-p", "--dump_port"):
        arg_port = arg
    elif opt in ("-U", "--dump_username"):
        arg_username = arg
    elif opt in ("-o", "--output"):
        arg_output = arg
    elif opt in ("--pg_dump_path"):
        arg_pgdump = arg
    elif opt in ("--Fp"):
        arg_fp = 1
    elif opt in ("-n", "--nohashfile"):
        arg_nohashfile = 1
    elif opt in ("--nodrop"):
        arg_nodrop = 1
    elif opt in ("-v", "--verbose"):
        arg_verbose = 1

if arg_schema == "":
    print "--schema (-n) argument is required"
    sys.exit(2)

if os.path.exists(arg_output) == False:
    print "Path given by --output (-o) does not exist"
    sys.exit(2)


def get_tables():
    conn = psycopg2.connect(arg_connection)
    cur = conn.cursor()
    sql = "SELECT tablename FROM pg_catalog.pg_tables WHERE schemaname = %s";
    cur.execute(sql, [arg_schema])
    result = cur.fetchall()
    cur.close()
    conn.close()
    return result


def perform_dump(result):
    table_name = result.pop()[0]
    processcmd = []
    if arg_pgdump != "":
        processcmd.append(arg_dump)
    else:
        processcmd.append("pg_dump")
    if arg_host != "":
        processcmd.append("--host=" + arg_host)
    if arg_port != "":
        processcmd.append("--port=" + arg_port)
    if arg_username != "":
        processcmd.append("--username=" + arg_username)
    if arg_fp == 1:
        processcmd.append("--format=plain")
    else:
        processcmd.append("--format=custom")
#    processcmd.append("--schema=" + arg_schema)
    processcmd.append("--table=" + arg_schema + "." + table_name)
    output_file = os.path.join(arg_output, arg_schema + "." + table_name + ".pgdump")
    processcmd.append("--file=" + output_file)
    if arg_database != "":
        processcmd.append(arg_database)
    
    if arg_verbose == 1:
        print processcmd 
    try:
        subprocess.check_call(processcmd)
    except subprocess.CalledProcessError, e:
        print "Error in pg_dump command: " + str(e.cmd)
        sys.exit(2)

    return table_name 


def create_hash(table_name):
    output_file = os.path.join(arg_output, arg_schema + "." + table_name + ".pgdump")
    try:
        with open(output_file, "rb") as fh:
            shash = hashlib.sha512()
            while True:
                data = fh.read(8192)
                if not data:
                    break
                shash.update(data)
    except IOError, (ErrorNo, ErrorMsg):
        print "Cannot access dump file for hash creation: " + ErrorMessage
        sys.exit(2)

    hash_file = os.path.join(arg_output, arg_schema + "." + table_name + ".hash")
    if arg_verbose == 1:
        print "hash_file: " + hash_file
    try:
        with open(hash_file, "w") as fh:
            fh.write(shash.hexdigest() + "  " + os.path.basename(output_file))
    except IOError, (ErroNo, ErrorMsg):
        print "Unable to write to hash file: " + ErrorMessage
        sys.exit(2)


def drop_table(table_name):
    conn = psycopg2.connect(arg_connection)
    cur = conn.cursor()
    sql = "DROP TABLE IF EXISTS " + arg_schema + "." + table_name;
    print sql 
    cur.execute(sql)
    conn.commit()
    cur.close()
    conn.close()

if __name__ == "__main__":
    result = get_tables()
    while len(result) > 0:
        table_name = perform_dump(result)
        if arg_nohashfile != 1:
            create_hash(table_name)
        if arg_nodrop != 1:
            drop_table(table_name)
