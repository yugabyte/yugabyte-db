# Install dependencies: https://github.com/YugaByte/cassandra-python-driver
# pip3 install yb-cassandra-driver

from cassandra.cluster import Cluster
import time
from functools import partial

from multiprocessing.dummy import Pool as ThreadPool
# START CONFIGURATION
# Which cluster?
cluster = Cluster(['127.0.0.1'])
# Which keyspace?
keyspace_name = "ybdemo"
# How many sub-tasks?
num_tasks_per_table = 64
# Max parallel processes?
num_parallel_tasks = 8
# END CONFIGURATION

session = cluster.connect()


def check_partition_row_count(keyspace_name, table_name, p_columns, l_bound):
    stmt = ("SELECT count(*) as rows FROM {}.{} " +
            "WHERE partition_hash({}) >= ? " +
            "AND partition_hash({}) <= ?").format(keyspace_name,
                                                  table_name,
                                                  p_columns,
                                                  p_columns)

    range_size = int((64 * 1024) / num_tasks_per_table)
    u_bound = l_bound + range_size - 1

    prepared_stmt = session.prepare(stmt)
    results = session.execute(prepared_stmt, (int(l_bound), int(u_bound)))
    row_cnt = results[0].rows
    print("Row Count for {}.{} partition({}, {}) = {}".format(keyspace_name, table_name, l_bound, u_bound, row_cnt))
    return row_cnt


def check_table_row_counts(keyspace_name, table_name):
    print("Checking row counts for: " + keyspace_name + "." + table_name)
    results = session.execute(("SELECT column_name, position  " +
                               "FROM system_schema.columns " +
                               "WHERE keyspace_name = %s AND table_name = %s " +
                               """AND kind='partition_key' """),
                              (keyspace_name, table_name))

    # Add the partition columns to an array sorted by the
    # position of the column in the primary key.
    partition_columns = [''] * 256
    num_partition_columns = 0
    for row in results:
        partition_columns[row.position] = row.column_name
        num_partition_columns = num_partition_columns + 1
    del partition_columns[num_partition_columns:]  # remove extra null elements from array

    p_columns = ",".join(partition_columns)
    print("Partition columns for " + keyspace_name + "." + table_name + ": (" + p_columns + ")")
    print("Performing {} checks for {}.{}".format(num_tasks_per_table, keyspace_name, table_name))

    range_size = int((64 * 1024) / num_tasks_per_table)
    l_bounds = []
    for idx in range(num_tasks_per_table):
        l_bound = int(idx * range_size)
        l_bounds.append(l_bound)

    pool = ThreadPool(num_parallel_tasks)
    t1 = time.time()
    row_counts = pool.map(partial(check_partition_row_count,
                                  keyspace_name,
                                  table_name,
                                  p_columns),
                          l_bounds)
    t2 = time.time()
    print("====================")
    print("Total Time: %s ms" % ((t2 - t1) * 1000))
    print("====================")

    total_row_cnt = 0
    for idx in range(len(row_counts)):
        total_row_cnt = total_row_cnt + row_counts[idx]

    print("Total Row Count for {}.{} = {}".format(keyspace_name, table_name, total_row_cnt))
    print("--------------------------------------------------------")


def check_keyspace_table_row_counts(keyspace_name):
    print("Checking table row counts for keyspace: " + keyspace_name)
    print("--------------------------------------------------------")
    results = session.execute("select table_name from system_schema.tables where keyspace_name = %s",
                              (keyspace_name,))
    for row in results:
        check_table_row_counts(keyspace_name, row.table_name)


# Main
if __name__ == '__main__':
    check_keyspace_table_row_counts(keyspace_name)
