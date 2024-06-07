# AWS

This is an analysis of the most cost-effective AWS machines to run YugabyteDB.

- 3 nodes of 16 cores each with replication factor 3, fully cached workload can handle 100K reads and 20K writes.
See [this performance report](https://github.com/yugabyte/yugabyte-db/blob/master/docs/yb-perf-0.9.5rc-Feb-13.md) for details.
- 3 nodes of 8 cores each with replication factor 3, fully uncached workload with 1.4TB total can handle77K read ops/sec.
See [this post](https://blog.yugabyte.com/achieving-sub-ms-latencies-on-large-data-sets-in-public-clouds-bf38d13ac42d) for more details.
- The app needs between 1TB and 2TB of storage per node.
- Analyzed many other machine types, concluded they are not as effective to run YugabyteDB

```
machine-type   vCPUs   memory    raw-cost    storage-cost      Total (1TB)    Total (2TB)
----------------------------------------------------------------------------------------
c5.2xlarge      8      16GiB    $248/month   +$100/TB/month    $350/month     $450/month
c5.4xlarge      16     32GiB    $496/month   +$100/TB/month    $600/month     $700/month
i3.2xlarge      8      61GiB    $455/month   +$0 (1.9TB disk)  $455/month     $455/month (1.9TB local SSD)
```

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/cloud-machine-types-analysis.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)
