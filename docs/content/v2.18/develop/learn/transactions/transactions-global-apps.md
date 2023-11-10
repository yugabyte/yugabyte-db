---
title: Performance Tuning Transactions for Global Applications in YSQL
headerTitle: Performance tuning for global applications in YSQL
linkTitle: Global applications
description: Learn how to speed up Transactions in multi-region deployments in YSQL.
menu:
  v2.18:
    identifier: transactions-global-apps-ysql
    parent: acid-transactions-1-ysql
    weight: 569
type: docs
---

The following best practices and tips can greatly improve the performance of transactions in [multi-region](../../../../explore/multi-region-deployments/) deployments.

## Place leaders in one region

In a multi-region setup, a transaction would have to reach out to the tablet leaders spread across multiple regions. In this scenario, the transaction can incur high inter-regional latencies that could multiply with the number of statements that have to travel cross-region.

Cross-region trips can be avoided by placing all the tablet leaders in one region using the [set_preferred_zones](../../../../admin/yb-admin/#set-preferred-zones) command in [yb-admin](../../../../admin/yb-admin/).

You can also do this by [marking the zones as Preferred](../../../../yugabyte-platform/manage-deployments/edit-universe/) on the **Edit Universe** page in [YugabyteDB Anywhere](../../../../yugabyte-platform/), or [setting the region as preferred](../../../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-multisync/#preferred-region) in YugabyteDB Managed.

## Read from followers

All reads in YugabyteDB are handled by the leader to ensure that applications fetch the latest data, even though the data is replicated to the followers. While replication is fast, it is not instantaneous, and the followers may not have the latest data at the read time. But in some scenarios, reading from the leader is not necessary. For example:

- The data does not change often (for example, a movie database).
- The application does not need the latest data (for example, reading yesterday's report).

In such scenarios, you can enable [follower reads](../../../../explore/ysql-language-features/going-beyond-sql/follower-reads-ysql/) to read from followers instead of going to the leader, which could be far away in a different region.

To enable follower reads, set the transaction to be [READ ONLY](../../../../api/ysql/the-sql-language/statements/txn_set/#read-only-mode) and turn on the YSQL parameter `yb_read_from_followers`. For example:

```plpgsql
SET yb_read_from_followers = true;
BEGIN TRANSACTION READ ONLY;
...
COMMIT;
```

This will read data from the closest follower or leader. As replicas may not be up-to-date with all updates, by design, this will return only stale data (default: 30s). This is the case even if the read goes to a leader. The staleness value can be changed using another setting like:

```plpgsql
SET yb_follower_read_staleness_ms = 10000; -- 10s
```

{{<note title="Note">}}
Follower reads only affect reads. All writes are still handled by the leader.
{{</note>}}

## Use identity indexes

Adding indexes is a common technique for speeding up queries. By adding all the columns needed in a query to create a [covering index](../../../../explore/indexes-constraints/covering-index-ysql/), you can perform index-only scans, where you don't need to scan the table, only the index. When the schema of your covering index is the same as the table, then it is known as an identity index.

If you are running applications from multiple regions, you can use identity indexes in conjunction with [tablespaces](../../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) in a multi-region cluster to greatly improve read latencies, as follows:

- Create different tablespaces with preferred leaders set to each region.
- Create identity indexes and attach them to each of the tablespaces.

This results in immediately consistent multiple identity indexes with local leaders, one in each region. Now applications running in a region do not have to go cross-region to the table leader in another region. Although this affects write latencies, as each update has to reach multiple indexes, read latencies are much lower because the reads go to the local identity index of the table.

## Learn more

- [Transaction error codes](../transactions-errorcodes-ysql/) - Various error codes returned during transaction processing.
- [Transaction error handling](../transactions-retries-ysql/) - Methods to handle various error codes to design highly available applications.
- [Transaction isolation levels](../../../../architecture/transactions/isolation-levels/) - Various isolation levels supported by YugabyteDB.
- [Concurrency control](../../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../../architecture/transactions/transaction-priorities/) - Priority buckets for transactions.
- [Transaction options](../../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - Options supported by transactions.
