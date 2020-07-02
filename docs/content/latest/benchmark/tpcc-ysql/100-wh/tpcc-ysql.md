### TPC-C Load Phase

Before starting the workload, you will need to load the data first. Make sure
to replace the IP addresses with that of the nodes in the cluster. Loader
threads allow us to configure the number of threads used to load the data. For
a 3 node c5d.4xlarge cluster, loader threads value of 48 was the most optimal.

```sh
$ ./tpccbenchmark --create=true --load=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --warehouses=100 \
  --loaderthreads 48
```

<table>
  <tbody>
    <tr>
      <td>Cluster</td>
      <td>3 nodes of type `c5d.4xlarge`</td>
    </tr>
    <tr>
      <td>Loader threads</td>
      <td>48</td>
    </tr>
    <tr>
      <td>Loading Time</td>
      <td>~1 hour</td>
    </tr>
    <tr>
      <td>Data Set Size</td>
      <td>~80 GB</td>
    </tr>
  </tbody>
</table>

Tune the --loaderthreads parameter for higher parallelism during the load, based on the number and type of nodes in the cluster. The value specified here, 48 threads, is optimal for a 3-node cluster of type c5d.4xlarge (16 vCPUs). For larger clusters, or machines with more vCPUs, increase this value accordingly

### TPC-C Execute Phase

You can then run the workload against the database as follows:

```sh
$ ./tpccbenchmark --execute=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --warehouses=100
```

You can also load and run the benchmark in a single step:
```sh
$ ./tpccbenchmark --create=true --load=true --execute=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --loaderthreads 48 \
  --warehouses=100
```

## 4. TPC-C Benchmark Results

Once the execution is done the TPM-C number along with the efficiency is printed.

```
18:01:57,149 (DBWorkload.java:894) INFO  - ======================================================================
18:01:57,149 (DBWorkload.java:895) INFO  - Rate limited reqs/s: Results(nanoSeconds=1800000367051, measuredRequests=85376) = 47.43110143909266 requests/sec
18:01:57,150 (DBWorkload.java:900) INFO  - Num New Order transactions : 38211, time seconds: 1800
18:01:57,150 (DBWorkload.java:901) INFO  - TPM-C: 1273
18:01:57,150 (DBWorkload.java:902) INFO  - Efficiency : 98.9891135303266
18:01:57,151 (DBWorkload.java:737) INFO  - Output Raw data into file: results/oltpbench.csv
```
