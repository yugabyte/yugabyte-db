---
title: Testing high scale workloads of TPC-C benchmark
headerTitle: Testing high scale workloads of TPC-C benchmark
linkTitle: Testing high scale workloads
headcontent: Understand how YugabyteDB performs with high scale workloads
menu:
  preview:
    identifier: tpcc-high-scale
    parent: tpcc
    weight: 300
type: docs
rightNav:
  hideH3: true
---

Workloads in TPC-C are defined by the number of warehouses the benchmark run will simulate. We will explore how YugabyteDb performs as the number of warehouses is increased.

## Get TPC-C binaries

First, you need the benchmark binaries. To download the TPC-C binaries, run the following commands:

```sh
$ wget https://github.com/yugabyte/tpcc/releases/latest/download/tpcc.tar.gz
$ tar -zxvf tpcc.tar.gz
$ cd tpcc
```

## Client machine

The client machine is where the benchmark is run from. An 8vCPU machine with at least 16GB memory is recommended. The following instance types are recommended for the client machine.

| vCPU |           AWS           |            AZURE             |            GCP            |
| ---- | ----------------------- | ---------------------------- | ------------------------- |
| 8    | {{<inst "c5.2xlarge">}} | {{<inst "Standard_F8s_v2">}} | {{<inst "n2-highcpu-8">}} |

## Cluster setup

The following cloud provider instance types are recommended for this test.

| vCPU |           AWS            |             AZURE             |             GCP             |
| ---- | ------------------------ | ----------------------------- | --------------------------- |
| 2    | {{<inst "m6i.large">}}   | {{<inst "Standard_D8s_v3">}}  | {{<inst "n2-standard-2">}}  |
| 8    | {{<inst "m6i.2xlarge">}} | {{<inst "Standard_D2s_v3">}}  | {{<inst "n2-standard-8">}}  |
| 16   | {{<inst "m6i.4xlarge">}} | {{<inst "Standard_D16s_v3">}} | {{<inst "n2-standard-16">}} |

<!-- begin: nav tabs -->
{{<nav/tabs list="local,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
<details> <summary>Set up a local cluster</summary>
{{<setup/local collapse="no">}}

</details>

Store the IP addresses of the nodes in a shell variable for use in further commands.

```bash
IPS=127.0.0.1,127.0.0.2,127.0.0.3
```

{{</nav/panel>}}

{{<nav/panel name="cloud">}}
{{<setup/cloud>}}

Store the IP addresses/public address of the cluster in a shell variable for use in further commands.

```bash
IPS=<cluster-name/IP>
```

{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

## Initialize the workloads

{{<tabpane text=true >}}
{{% tab header="10 warehouses" lang="10-wh" %}}

Before starting the workload, you need to load the data. Replace the IP addresses with that of the nodes in the cluster.

```sh
$ ./tpccbenchmark --create=true --nodes=${IPS}
```

```sh
$ ./tpccbenchmark --load=true --nodes=${IPS}
```

|        Cluster        | Loader threads | Loading time | Data set size |
| :-------------------- | :------------: | :----------- | :------------ |
| 3 nodes, type `2vCPU` |       10       | ~13 minutes  | ~20 GB        |

The loading time for ten warehouses on a cluster with 3 nodes of type 16vCPU is approximately 3 minutes.

{{% /tab %}}
{{% tab header="100 warehouses" lang="100-wh" %}}

Before starting the workload, you need to load the data. Replace the IP addresses with that of the nodes in the cluster.

```sh
$ ./tpccbenchmark --create=true --nodes=${IPS}
```

```sh
$ ./tpccbenchmark --load=true --nodes=${IPS} --warehouses=100 --loaderthreads 48
```

|       Cluster        | Loader threads | Loading time | Data set size |
| :------------------- | :------------- | :----------- | :------------ |
| 3 nodes, type 16vCPU | 48             | ~20 minutes  | ~80 GB        |

Tune the `--loaderthreads` parameter for higher parallelism during the load, based on the number and type of nodes in the cluster. The specified 48 threads value is optimal for a 3-node cluster of type 16vCPU. For larger clusters or computers with more vCPUs, increase this value accordingly. For clusters with a replication factor of 3, a good approximation is to use the number of cores you have across all the nodes in the cluster.

{{% /tab %}}
{{% tab header="1,000 warehouses" lang="1k-wh" %}}

Before starting the workload, you need to load the data first. Replace the IP addresses with that of the nodes in the cluster.

```sh
$ ./tpccbenchmark --create=true --nodes=${IPS}
```

```sh
$ ./tpccbenchmark --load=true --nodes=${IPS} --warehouses=1000 --loaderthreads 48
```

|       Cluster        | Loader threads | Loading time | Data set size |
| :------------------- | :------------- | :----------- | :------------ |
| 3 nodes, type 16vCPU | 48             | ~3.5 hours   | ~420 GB       |

Tune the `--loaderthreads` parameter for higher parallelism during the load, based on the number and type of nodes in the cluster. The specified 48 threads value is optimal for a 3-node cluster of type c5d.4xlarge. For larger clusters or computers with more vCPUs, increase this value accordingly. For clusters with a replication factor of 3, a good approximation is to use the number of cores you have across all the nodes in the cluster.

{{% /tab %}}
{{% tab header="10,000 warehouses" lang="10k-wh" %}}

Before starting the workload, you need to load the data. In addition, you need to ensure that you exported a list of all IP addresses of all the nodes involved.

For 10k warehouses, you would need ten clients of type `c5.4xlarge` to drive the benchmark. For multiple clients, you need to perform the following steps.

First, create the database and the corresponding tables. Execute the following command from one of the clients:

```sh
./tpccbenchmark  --nodes=$IPS  --create=true --vv
```

After the database and tables are created, load the data from all ten clients:

{{<note>}}
The initial-delay-secs is introduced to avoid a sudden overload on the master to fetch catalogs.
{{</note>}}

| Client | Command |
| -----: | :------ |
| 1  |./tpccbenchmark -c config/workload_all.xml --load=true --nodes=$IPs --warehouses=2500 --total-warehouses=10000 --loaderthreads=16 --start-warehouse-id=1 --initial-delay-secs=0 --vv
| 2  |./tpccbenchmark -c config/workload_all.xml --load=true --nodes=$IPs --warehouses=2500 --total-warehouses=10000 --loaderthreads=16 --start-warehouse-id=2501 --initial-delay-secs=30 --vv
| 3  |./tpccbenchmark -c config/workload_all.xml --load=true --nodes=$IPs --warehouses=2500 --total-warehouses=10000 --loaderthreads=16 --start-warehouse-id=5001 --initial-delay-secs=60 --vv
| 4  |./tpccbenchmark -c config/workload_all.xml --load=true --nodes=$IPs --warehouses=2500 --total-warehouses=10000 --loaderthreads=16 --start-warehouse-id=7501 --initial-delay-secs=90 --vv

Tune the `--loaderthreads` parameter for higher parallelism during the load, based on the number and type of nodes in the cluster. The value specified, 48 threads, is optimal for a 3-node cluster of type 16vCPU. For larger clusters or computers with more vCPUs, increase this value accordingly. For clusters with a replication factor of 3, a good approximation is to use the number of cores you have across all the nodes in the cluster.

When the loading is completed, execute the following command to enable the foreign keys that were disabled to aid the loading times:

```sh
./tpccbenchmark  --nodes=$IPS  --enable-foreign-keys=true
```

|        Cluster        | Loader threads | Loading time | Data set size |
| :-------------------- | :------------- | :----------- | :------------ |
| 30 nodes, type 16vCPU | 480            | ~5.5 hours   | ~4 TB         |

{{% /tab %}}
{{</tabpane >}}

## Run the benchmark

{{<tabpane text=true >}}
{{% tab header="10 warehouses" lang="10-wh" %}}

You can run the workload against the database as follows:

```sh
$ ./tpccbenchmark --execute=true --warmup-time-secs=60 --nodes=${IPS}
```

{{% /tab %}}
{{% tab header="100 warehouses" lang="100-wh" %}}

You can run the workload against the database as follows:

```sh
$ ./tpccbenchmark --execute=true --warmup-time-secs=60 --nodes=${IPS} --warehouses=100
```

{{% /tab %}}
{{% tab header="1,000 warehouses" lang="1k-wh" %}}

You can run the workload against the database as follows:

```sh
$ ./tpccbenchmark --execute=true --warmup-time-secs=60 --nodes=${IPS} --warehouses=1000
```

{{% /tab %}}
{{% tab header="10,000 warehouses" lang="10k-wh" %}}

You can then run the workload against the database from each client:

{{<note>}}
As you specify initial-delay-secs to ensure that the tests start together, adjust that time in the warmup phase.
{{</note>}}

| Client | Command |
| -----: | :------ |
| 1  |./tpccbenchmark -c config/workload_all.xml --execute=true --nodes=$IPs --warehouses=2500 --start-warehouse-id=1 --total-warehouses=10000  --initial-delay-secs=0 --warmup-time-secs=900  --num-connections=90
| 2  |./tpccbenchmark -c config/workload_all.xml --execute=true --nodes=$IPs --warehouses=2500 --start-warehouse-id=2501 --total-warehouses=10000  --initial-delay-secs=30 --warmup-time-secs=870  --num-connections=90
| 3  |./tpccbenchmark -c config/workload_all.xml --execute=true --nodes=$IPs --warehouses=2500 --start-warehouse-id=5001 --total-warehouses=10000  --initial-delay-secs=60 --warmup-time-secs=840  --num-connections=90
| 4  |./tpccbenchmark -c config/workload_all.xml --execute=true --nodes=$IPs --warehouses=2500 --start-warehouse-id=7501 --total-warehouses=10000  --initial-delay-secs=90 --warmup-time-secs=810  --num-connections=90

{{% /tab %}}
{{</tabpane >}}

## Benchmark results

{{<tabpane text=true >}}
{{% tab header="10 warehouses" lang="10-wh" %}}

|                    Metric                    | Value  |
| -------------------------------------------- | :----- |
| Efficiency                                   | 97.85  |
| TPMC                                         | 125.83 |
| Average NewOrder Latency (ms)                | 19.2   |
| Connection Acquisition NewOrder Latency (ms) | 0.72   |
| Total YSQL Ops/sec                           | 39.51  |

{{% /tab %}}
{{% tab header="100 warehouses" lang="100-wh" %}}

|                    Metric                    |  Value  |
| -------------------------------------------- | :------ |
| Efficiency                                   | 99.31   |
| TPMC                                         | 1277.13 |
| Average NewOrder Latency (ms)                | 10.36   |
| Connection Acquisition NewOrder Latency (ms) | 0.27    |
| Total YSQL Ops/sec                           | 379.78  |

{{% /tab %}}
{{% tab header="1,000 warehouses" lang="1k-wh" %}}

|                    Metric                    |  Value   |
| -------------------------------------------- | :------- |
| Efficiency                                   | 100.01   |
| TPMC                                         | 12861.13 |
| Average NewOrder Latency (ms)                | 16.09    |
| Connection Acquisition NewOrder Latency (ms) | 0.38     |
| Total YSQL Ops/sec                           | 3769.86  |

{{% /tab %}}
{{% tab header="10,000 warehouses" lang="10k-wh" %}}

When the execution is completed, you need to copy the `csv` files from each of the nodes to one of the nodes and run `merge-results` to display the merged results.

After copying the `csv` files to a directory such as `results-dir`, merge the results as follows:

```sh
./tpccbenchmark --merge-results=true --dir=results-dir --warehouses=10000
```

|                    Metric                    |   Value   |
| -------------------------------------------- | :-------- |
| Efficiency                                   | 99.92     |
| TPMC                                         | 128499.53 |
| Average NewOrder Latency (ms)                | 26.4      |
| Connection Acquisition NewOrder Latency (ms) | 0.44      |
| Total YSQL Ops/sec                           | 37528.64  |

{{% /tab %}}
{{</tabpane >}}

## Largest benchmark

In our testing, YugabyteDB was able to process 1M tpmC with 150,000 warehouses at an efficiency of 99.8% on an RF3 cluster of 75 48vCPU/96GB machines with a total data size of 50TB.

To accomplish this feat, numerous new features had to be implemented and existing ones optimized, including the following:

- The number of RPCs made was reduced to reduce CPU usage.
- Fine-grained locking was implemented.
- Index updates were optimized by skipping index updates when columns in the index were not updated.
- An encoded table's primary key was used as the pointer in the index.
- Continuous background compaction.
- Hot data was stored in the block cache.
- Transaction retries.

{{<note>}}
The 150K warehouses benchmark was run on [v2.11](/preview/releases/ybdb-releases/end-of-life/v2.11/).
{{</note>}}

| Warehouses | TPMC | Efficiency(%) | Nodes | Connections | New Order Latency |  Machine Type (vCPUs)  |
| ---------: | :--- | :-----------: | :---: | ----------- | :---------------: | :--------------------- |
|    150,000 | 1M   |     99.30     |  75   | 9000        |     123.33 ms     | {{<inst "c5d.12xlarge">}}&nbsp;(48) |
