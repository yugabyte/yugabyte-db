### TPC-C Load Phase

Before starting the workload, you will need to load the data first. Make sure
to replace the IP addresses with that of the nodes in the cluster.

```sh
$ ./tpccbenchmark --create=true --load=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3
```

<table>
  <tbody>
    <tr>
      <td>Cluster</td>
      <td>3 nodes of type `c5.large`</td>
    </tr>
    <tr>
      <td>Loader threads</td>
      <td>10</td>
    </tr>
    <tr>
      <td>Loading Time</td>
      <td>~40 minutes</td>
    </tr>
    <tr>
      <td>Data Set Size</td>
      <td>~20 GB</td>
    </tr>
  </tbody>
</table>

---
**NOTE**

The loading time for 10 warehouses on a cluster with 3 nodes of type c5d.4xlarge is around 11 minutes.

---

### TPC-C Execute Phase

You can then run the workload against the database as follows:

```sh
$ ./tpccbenchmark --execute=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3
```

You can also load and run the benchmark in a single step:
```sh
$ ./tpccbenchmark --create=true --load=true --execute=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3
```

## 4. TPC-C Benchmark Results

Once the execution is done the TPM-C number along with the efficiency is printed.

```
22:51:04,125 (DBWorkload.java:895) INFO  - Rate limited reqs/s: Results(nanoSeconds=1800000323771, measuredRequests=8536) = 4.742221369225692 requests/sec
22:51:04,125 (DBWorkload.java:900) INFO  - Num New Order transactions : 3839, time seconds: 1800
22:51:04,126 (DBWorkload.java:901) INFO  - TPM-C: 127
22:51:04,126 (DBWorkload.java:902) INFO  - Efficiency : 98.75583203732505
22:51:04,127 (DBWorkload.java:737) INFO  - Output Raw data into file: results/oltpbench.csv
```
