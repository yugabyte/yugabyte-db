## TPC-C Load Phase

Before starting the workload, you need to load the data. In addition, you need to ensure that you exported a list of all IP addresses of all the nodes involved.

For 10k warehouses, you would need ten clients of type c5.2xlarge to drive the benchmark.
For multiple clients, you need to perform three steps.

First, you create the database and the corresponding tables. Execute the following command from one of the clients:

```sh
./tpccbenchmark  --nodes=$IPS  --create=true
```

Once the database and tables are created, you can load the data from all ten clients:


| Client | Command
-------------|-----------|
1  |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=1    --total-warehouses=10000 --loaderthreads 48
2  |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=1001 --total-warehouses=10000 --loaderthreads 48
3  |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=2001 --total-warehouses=10000 --loaderthreads 48
4  |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=3001 --total-warehouses=10000 --loaderthreads 48
5  |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=4001 --total-warehouses=10000 --loaderthreads 48
6  |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=5001 --total-warehouses=10000 --loaderthreads 48
7  |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=6001 --total-warehouses=10000 --loaderthreads 48
8  |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=7001 --total-warehouses=10000 --loaderthreads 48
9  |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=8001 --total-warehouses=10000 --loaderthreads 48
10 |  ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=9001 --total-warehouses=10000 --loaderthreads 48

Tune the `--loaderthreads` parameter for higher parallelism during the load, based on the number and type of nodes in the cluster. The value specified here, 48 threads, is optimal for a 3-node cluster of type c5d.4xlarge (16 vCPUs). For larger clusters, or computers with more vCPUs, increase this value accordingly. For clusters with a replication factor of 3, a good approximation is to use the number of cores you have across all the nodes in the cluster.
Once the loading is completed, execute the following command to enable the foreign keys that were disabled to aid the loading times:

```sh
./tpccbenchmark  --nodes=$IPS  --enable-foreign-keys=true
```

<table>
  <tbody>
    <tr>
      <td>Cluster</td>
      <td>30 nodes of type `c5d.4xlarge`</td>
    </tr>
    <tr>
      <td>Loader threads</td>
      <td>480</td>
    </tr>
    <tr>
      <td>Loading Time</td>
      <td>~5.5 hours</td>
    </tr>
    <tr>
      <td>Data Set Size</td>
      <td>~4 TB</td>
    </tr>
  </tbody>
</table>

## TPC-C Execute Phase

Before starting the execution, you have to move all the tablet leaders out of the node containing the master leader by running the following command:

```sh
./yb-admin --master_addresses <master-ip1>:7100,<master-ip2>:7100,<master-ip3>:7100 change_leader_blacklist ADD <master-leader-ip>
```

Make sure that the IPS used in the execution phase does not include the `master-leader-ip`.
You can then run the workload against the database from each client:

| Client | Command
-------------|-----------|
1  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=1    --total-warehouses=10000 --warmup-time-secs=900
2  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=1001 --total-warehouses=10000 --warmup-time-secs=900
3  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=2001 --total-warehouses=10000 --warmup-time-secs=900
4  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=3001 --total-warehouses=10000 --warmup-time-secs=900
5  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=4001 --total-warehouses=10000 --warmup-time-secs=900
6  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=5001 --total-warehouses=10000 --warmup-time-secs=720 --initial-delay-secs=180
7  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=6001 --total-warehouses=10000 --warmup-time-secs=540 --initial-delay-secs=360
8  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=7001 --total-warehouses=10000 --warmup-time-secs=360 --initial-delay-secs=540
9  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=8001 --total-warehouses=10000 --warmup-time-secs=180 --initial-delay-secs=720
10 | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=9001 --total-warehouses=10000 --warmup-time-secs=0   --initial-delay-secs=900

## TPC-C Benchmark Results

When the execution is completed, you need to copy the `csv` files from each of the nodes to one of the nodes and run `merge-results` to display the merged results.
Once you copied the `csv` files to a directory such as `results-dir`, you can merge the results as follows:

```sh
./tpccbenchmark --merge-results=true --dir=results-dir --warehouses=10000
```

<table>
  <tbody>
    <tr>
      <td>Cluster</td>
      <td>30 nodes of type `c5d.4xlarge`</td>
    </tr>
    <tr>
      <td>TPMC</td>
      <td>125193.2</td>
    </tr>
    <tr>
      <td>Efficiency</td>
      <td>97.35%</td>
    </tr>
    <tr>
      <td>Latencies</td>
      <td>
        New Order<br />
        Avg: 114.639 msecs, p99: 852.183 msecs<br />
        Payment<br />
        Avg: 114.639 msecs, p99 : 852.183 msecs<br />
        OrderStatus<br />
        Avg: 20.86 msecs, p99: 49.31 msecs<br />
        Delivery<br />
        Avg: 117.473 msecs, p99: 403.404 msecs<br />
        StockLevel<br />
        Avg: 340.232 msecs, p99: 1022.881 msecs
      </td>
    </tr>
  </tbody>
</table>

The output after merging should look similar to the following:


```
15:16:07,397 (DBWorkload.java:715) INFO - Skipping benchmark workload execution
15:16:11,400 (DBWorkload.java:1080) INFO - Num New Order transactions : 3779016, time seconds: 1800
15:16:11,400 (DBWorkload.java:1081) INFO - TPM-C: 125193.2
15:16:11,401 (DBWorkload.java:1082) INFO - Efficiency : 97.35%
15:16:12,861 (DBWorkload.java:1010) INFO - NewOrder, Avg Latency: 114.639 msecs, p99 Latency: 852.183 msecs
15:16:13,998 (DBWorkload.java:1010) INFO - Payment, Avg Latency: 29.351 msecs, p99 Latency: 50.8 msecs
15:16:14,095 (DBWorkload.java:1010) INFO - OrderStatus, Avg Latency: 20.86 msecs, p99 Latency: 49.31 msecs
15:16:14,208 (DBWorkload.java:1010) INFO - Delivery, Avg Latency: 117.473 msecs, p99 Latency: 403.404 msecs
15:16:14,310 (DBWorkload.java:1010) INFO - StockLevel, Avg Latency: 340.232 msecs, p99 Latency: 1022.881 msecs
```
