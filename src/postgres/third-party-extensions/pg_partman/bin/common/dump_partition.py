#!/usr/bin/env python

import argparse, hashlib, os, os.path, psycopg2, subprocess, sys

partman_version = "2.0.0"

parser = argparse.ArgumentParser(description="This script will dump out and then drop all tables contained in the designated schema using pg_dump.  Each table will be in its own separate file along with a SHA-512 hash of the dump file. Tables are not dropped from the database if pg_dump does not return successfully. All dump_* option defaults are the same as they would be for pg_dump if they are not given.", epilog="NOTE: The connection options for psyocpg and pg_dump were separated out due to distinct differences in their requirements depending on your database connection configuration.")
parser.add_argument('-n','--schema', help="(Required) The schema that contains the tables that will be dumped.")
parser.add_argument('-c','--connection', default="host=", help="""Connection string for use by psycopg. Role used must be able to select pg_catalog.pg_tables in the relevant database and drop all tables in the given schema.  Defaults to "host=" (local socket). Note this is distinct from the parameters sent to pg_dump.""")
parser.add_argument('-o','--output', default=os.getcwd(), help="Path to dump file output location. Default is where the script is run from.")
parser.add_argument('-d','--dump_database', help="Used for pg_dump, same as its --dbname (-d) option or final database name parameter.")
parser.add_argument('--dump_host', help="Used for pg_dump, same as its --host (-h) option.")
parser.add_argument('--dump_username', help="Used for pg_dump, same as its --username (-U) option.")
parser.add_argument('--dump_port', help="Used for pg_dump, same as its --port (-p) option.")
parser.add_argument('--pg_dump_path', help="Path to pg_dump binary location. Must set if not in current PATH.")
parser.add_argument('--Fp', action="store_true", help="Dump using pg_dump plain text format. Default is binary custom (-Fc).")
parser.add_argument('--nohashfile', action="store_true", help="Do NOT create a separate file with the SHA-512 hash of the dump. If dump files are very large, hash generation can possibly take a long time.")
parser.add_argument('--nodrop', action="store_true", help="Do NOT drop the tables from the given schema after dumping/hashing.")
parser.add_argument('-v','--verbose', action="store_true", help="Provide more verbose output.")
parser.add_argument('--version', action="store_true", help="Print out the minimum version of pg_partman this script is meant to work with. The version of pg_partman installed may be greater than this.")
args = parser.parse_args()


def create_hash(table_name):
    output_file = os.path.join(args.output, args.schema + "." + table_name + ".pgdump")
    try:
        with open(output_file, "rb") as fh:
            shash = hashlib.sha512()
            while True:
                data = fh.read(8192)
                if not data:
                    break
                shash.update(data)
    except IOError as e:
        print("Cannot access dump file for hash creation: " + e.strerror)
        sys.exit(2)

    hash_file = os.path.join(args.output, args.schema + "." + table_name + ".hash")
    if args.verbose:
        print("hash_file: " + hash_file)
    try:
        with open(hash_file, "w") as fh:
            fh.write(shash.hexdigest() + "  " + os.path.basename(output_file))
    except IOError as e:
        print("Unable to write to hash file: " + e.strerror)
        sys.exit(2)


def drop_table(table_name):
    conn = psycopg2.connect(args.connection)
    cur = conn.cursor()
    sql = "DROP TABLE IF EXISTS \"" + args.schema + "\".\"" + table_name + "\"";
    print(sql)
    cur.execute(sql)
    conn.commit()
    cur.close()
    conn.close()


def get_tables():
    conn = psycopg2.connect(args.connection)
    cur = conn.cursor()
    sql = "SELECT tablename FROM pg_catalog.pg_tables WHERE schemaname = %s";
    cur.execute(sql, [args.schema])
    result = cur.fetchall()
    cur.close()
    conn.close()
    return result


def perform_dump(result):
    table_name = result.pop()[0]
    processcmd = []
    if args.pg_dump_path != None:
        processcmd.append(args.pg_dump_path)
    else:
        processcmd.append("pg_dump")
    if args.dump_host != None:
        processcmd.append("--host=" + args.dump_host)
    if args.dump_port != None:
        processcmd.append("--port=" + args.dump_port)
    if args.dump_username != None:
        processcmd.append("--username=" + args.dump_username)
    if args.Fp:
        processcmd.append("--format=plain")
    else:
        processcmd.append("--format=custom")
    processcmd.append("--table=\"" + args.schema + "\".\"" + table_name + "\"")
    output_file = os.path.join(args.output, args.schema + "." + table_name + ".pgdump")
    processcmd.append("--file=" + output_file)
    if args.dump_database != None:
        processcmd.append(args.dump_database)

    if args.verbose:
        print(processcmd)
    try:
        subprocess.check_call(processcmd)
    except subprocess.CalledProcessError as e:
        print("Error in pg_dump command: " + str(e.cmd))
        sys.exit(2)

    return table_name 


def print_version():
    print(partman_version)
    sys.exit()


if __name__ == "__main__":

    if args.version:
        print_version()

    if args.schema == None:
        print("-n/--schema option is required")
        sys.exit(2)

    if not os.path.exists(args.output):
        print("Path given by --output (-o) does not exist: " + str(args.output))
        sys.exit(2)

    result = get_tables()
    while len(result) > 0:
        table_name = perform_dump(result)
        if not args.nohashfile:
            create_hash(table_name)
        if not args.nodrop:
            drop_table(table_name)
