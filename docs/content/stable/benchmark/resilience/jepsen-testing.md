---
title: Jepsen testing
headerTitle: Jepsen testing
linkTitle: Jepsen testing
description: Description of Jepsen testing.
menu:
  stable:
    identifier: jepsen-testing-1
    parent: resilience
    weight: 13
type: docs
---

This page describes Jepsen tests that YugabyteDB runs daily for each major release currently available.

## Scenarios

### Bank

This test simulates money transfer between accounts; it uses a table T with schema `(id int PRIMARY KEY, balance bigint)`. The workload performs transactional transfers between accounts - `(UPDATE Tbalance = balance - x WHERE id = ?; UPDATE T SET balance = balance + x WHERE id = ?;)`as well as reads of the whole table. The grand total is expected to remain the same, both in short and long term.

![Load Phase Results](/images/benchmark/jepsen/jepsen-1-bank.png)

```plpgsql
BEGIN TXN;
UPDATE T SET balance = balance + x WHERE id = ?;
UPDATE T SET balance = balance - x WHERE id = ?;
COMMIT;
```

### Bank-contention (YSQL)

In addition to UPDATE transactions, there are also INSERT or INSERT/DELETE transactions, all operating under the assumption that the overall `SUM(balance)` must remain consistent. To avoid overcomplicating the scenario in the case of Jepsen, 5 keys designated for UPDATE operations and 5 keys for random INSERTs and DELETEs

![Load Phase Results](/images/benchmark/jepsen/jepsen-2-bank-contention.png)

```plpgsql
-- updates
BEGIN TXN;
UPDATE T SET balance = balance + x WHERE id = ?;
UPDATE T SET balance = balance - x WHERE id = ?;
COMMIT;

-- inserts
BEGIN TXN;
INSERT INTO T (id, balance) VALUES (?, ?);
UPDATE T SET balance = balance - x WHERE id = ?;
COMMIT;

-- deletes
BEGIN TXN;
SELECT balance FROM T WHERE id = ?;
UPDATE T SET balance = balance + x WHERE id = ?;
DELETE FROM T WHERE id = ?;
COMMIT;
```

### Counter

This test uses a table T with schema `(id int PRIMARY KEY, count int)` with a single row, with the workload consisting of concurrent increments `(UPDATE T SET count = count + ? WHERE id = 0)` and reads. At any given time the value column is expected to be not more than the number of increments issued, and not less than the number of increments succeeded.

The test utilizes the int column type for YSQL and counter type for YCQL.

![Load Phase Results](/images/benchmark/jepsen/jepsen-3-counter.png)

### Set

This test uses a table T with schema `(id int PRIMARY KEY, val int, grp int)`. Concurrently, values are inserted into this table while simultaneously reading the entire table. Once an insert operation succeeds, or when an element is first observed in a read operation (whichever occurs first), all subsequent reads are expected to include that inserted element.

For YCQL, the table also includes a count column, and the workload permits duplicate inserts.

![Load Phase Results](/images/benchmark/jepsen/jepsen-4-set.png)

```plpgsql
INSERT INTO T (id, val, grp) VALUES (?, ?, ?);
SELECT val FROM T where id = 0;
```

### Long fork

The long-fork test uses a table T with schema `(key int PRIMARY KEY, key2 int, val int)`, where individual workers execute either single-row inserts or perform multi-row reads. The expectation is that the read results are serializable. This means that for two write operations, W1 and W2, it should not be possible for a read operation R1 to observe write W1 but not W2, while another read operation R2 observes W2 but not W1.

To read more about the long-fork test see [here](https://jepsen-io.github.io/jepsen/jepsen.tests.long-fork.html).

![Load Phase Results](/images/benchmark/jepsen/jepsen-5-long-fork.png)

### Default value

This scenario entails concurrent Data Definition Language (DDL) and Data Manipulation Language (DML) operations, simulating a migration scenario. Here, the user adds a `DEFAULT 0` column, ensuring that it is actually zero and not null when performing inserts and reads.

![Load Phase Results](/images/benchmark/jepsen/jepsen-6-default-value.png)

### Single Key ACID

The test uses a table T with schema `(id int PRIMARY KEY, val int)` with a fixed number of rows, each row having several worker threads assigned to it. Each worker can either read the row, update the row, or perform compare-and-set `UPDATE T SET val = ? WHERE id = ? AND val = ?`, worker groups for different rows are completely independent of each other. Checker makes sure that the resulting operations history is linearizable - i.e. that reads observe previous writes and writes don’t disappear.

![Load Phase Results](/images/benchmark/jepsen/jepsen-7-single-key-acid.png)

### Multi Key ACID

This test is similar to the single-key ACID test, but uses a composite primary key on table T with schema `(k1 int, k2 int, val int, PRIMARY KEY (k1, k2))` and UPSERTs (for YSQL it is `INSERT .. ON CONFLICT DO UPDATE SET`) instead of compare-and-set.

```plpgsql
INSERT INTO T VALUES (k1, k2, val) ON CONFLICT DO UPDATE SET val = ?;
SELECT k1, value FROM T where k2 = ? and k1 = ?;
```

### Append

In addition to a usual integer primary key, the table schema for this test uses a few text columns that hold comma-separated integers. Workers perform small transactions - a mix of concatenated updates like `(UPDATE T SET txt = CONCAT(txt, ',', ?) WHERE id = ?)` and single-row reads. It then verifies the history, looking for various serializable isolation anomalies (these are complex and are abbreviated as G0, G1, and G2; see [Generalized Isolation Level Definitions](http://pmg.csail.mit.edu/papers/icde00.pdf) for a suggested reading).

Unlike other tests, this one uses several identical tables rather than just one. More information about this test is available in the [CMU Quarantine Tech Talks: Black-box Isolation Checking with Elle](https://www.youtube.com/watch?v=OPJ_IcdSqig) (Kyle Kingsbury, Jepsen.io).

These tests utilize geo-partitioning, different row level locking modes and all isolation types currently supported in YugabyteDB.

![Load Phase Results](/images/benchmark/jepsen/jepsen-8-append.png)

## Nemesis

In our daily testing, an [LXC configuration](https://linuxcontainers.org/lxc/introduction/) is used with a 5-node setup. The original nemesis list as not significantly expanded, except for the inclusion of clock skew by default during the initialization of yb-tserver/yb-master processes. VM nemeses (AWS VM restart/start/stop, volume detach) and some stress OS level nemeses (using stress-ng, network slowdown) are covered in our stress framework.

The complete nemeses list includes:

### Software Clock Skew

YugabyteDB has special Gflag options that can utilize software clock skew. This nemesis assigns random clock skew values to each tserver or master process in scenarios where process restart is used.

### Restart of yb-master or yb-tserver process

Nemesis uses the ps utility to identify master or tserver processes and then use the `kill -9`command to forcefully stop and restart them.

### Pause of yb-master or yb-tserver process

Instead of killing the process, a STOP signal is sent to it, which may lead to tricky behavior between node interactions using the same `kill` utility as before.

### Network Partitioning

This nemesis uses the `iptables` utility to drop connectivity between testing nodes.

```shell
# drop connection from current node to 10.20.30.40,10.20,30.41
iptables -A INPUT -s 10.20.30.40,10.20,30.41 -j DROP -w

# heal everything
iptables -F -w
iptables -X -w
```

### Combination of Kill, Partition, and Pause Nemeses

Here is an example of how it works in combination during the test. Notice that there is some space for no nemesis so the test can execute and achieve some successful operations. The test fails if an additional check shows that the number of write or read operations is zero.

![Load Phase Results](/images/benchmark/jepsen/jepsen-9-nemesis-combine.png)

## Known issues

All issues related to Jepsen are tracked in our GitHub issues [here](https://github.com/yugabyte/yugabyte-db/issues/10052), latest info can be found there.
