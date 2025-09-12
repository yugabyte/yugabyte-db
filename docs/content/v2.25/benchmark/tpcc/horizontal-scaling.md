---
title: Testing horizontal scalability with TPC-C
headerTitle: Testing horizontal scalability with TPC-C
linkTitle: Testing horizontal scalability
menu:
  preview:
    identifier: tpcc-horizontal-scalability
    parent: tpcc
    weight: 200
type: docs
rightNav:
  hideH3: true
---

YugabyteDB sustains efficiency and maintains linear growth as a cluster is augmented with more nodes, each node contributing its full processing power to the collective performance. With an efficiency score of around 99.7%, nearly every CPU cycle is effectively used for transaction processing, with minimal overhead.

The following table describes how YugabyteDB horizontally scales with the TPC-C workload.

| Nodes | vCPUs | Warehouses |   TPMC   | Efficiency(%) | Connections | New Order Latency |
| :---: | :---: | :--------: | :------- | :-----------: | :---------: | :---------------: |
|   3   |  24   |    500     | 25646.4  |     99.71     |     200     |     54.21 ms      |
|   4   |  32   |    1000    | 34212.57 |     99.79     |     266     |     53.92 ms      |
|   5   |  40   |    2000    | 42772.6  |     99.79     |     333     |     51.01 ms      |
|   6   |  48   |    4000    | 51296.9  |     99.72     |     400     |     62.09 ms      |

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

We will use 8vCPU machines for this test. The following cloud provider instance types are recommended for this test.

| vCPU |           AWS            |             AZURE             |            GCP             |
| ---- | ------------------------ | ----------------------------- | -------------------------- |
| 8    | {{<inst "m6i.2xlarge">}} | {{<inst "Standard_D8ds_v5">}} | {{<inst "n2-standard-8">}} |

<!-- begin: nav tabs -->
{{<nav/tabs list="local,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local>}}

Store the IP addresses of the nodes in a shell variable for use in further commands.

```bash
IPS=127.0.0.1,127.0.0.2,127.0.0.3
```

{{</nav/panel>}}

{{<nav/panel name="cloud">}}
{{<setup/cloud>}}
{{<warning title="Adding nodes">}}
For the horizontal scale test, set the fault tolerance level to **None** so that you can add a single node to the cluster.
{{</warning>}}

Store the IP addresses/public address of the cluster in a shell variable for use in further commands.

```bash
IPS=<cluster-name/IP>
```

{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

## Benchmark the 3-node cluster

To run the benchmark, do the following:

1. Initialize the database needed for the benchmark by following the instructions specific to your cluster.

    Set up the TPC-C database schema with the following command:

    ```sh
    $ ./tpccbenchmark --create=true --nodes=${IPS}
    ```

    Populate the database with data needed for the benchmark using the following command:

    ```sh
    $ ./tpccbenchmark --load=true --nodes=${IPS} --warehouses=2000 --loaderthreads 20
    ```

1. Run the benchmark using the following command:

    ```sh
    $ ./tpccbenchmark --execute=true --warmup-time-secs=300 --nodes=${IPS} --warehouses=2000 --num-connections=200
    ```

1. Gather the results.

    | Nodes | vCPUs | Warehouses |   TPMC   | Efficiency(%) | Connections | New Order Latency |
    | :---: | :---: | :--------: | :------- | :-----------: | :---------: | :---------------: |
    |   3   |  24   |    500     | 25646.4  |     99.71     |     200     |     54.21 ms      |

1. Clean up the test run using the following command:

    ```sh
    $ ./tpccbenchmark --clear=true --nodes=${IPS} --warehouses=2000
    ```

## Add the 4th node

<!-- begin: nav tabs -->
{{<nav/tabs list="local,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local numnodes="1" masterip="127.0.0.1" ips="127.0.0.4" dirnum="4" locations="aws.us-east.us-east-1a" alias="no" instructions="no" destroy="no" dataplacement="no" status="no" collapse="Add a node">}}

Add the new IP address to the existing variable.

```bash
IPS=${IPS},127.0.0.4
```

{{</nav/panel>}}
{{<nav/panel name="cloud">}}
Add a node using the [Edit Infrastructure](../../../yugabyte-cloud/cloud-clusters/configure-clusters/) option and increase the node count by 1.
{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

Re-run the test as follows:

1. Initialize the database needed for the benchmark by following the instructions specific to your cluster.

    Set up the TPC-C database schema with the following command:

    ```sh
    $ ./tpccbenchmark --create=true --nodes=${IPS}
    ```

    Populate the database with data needed for the benchmark with the following command:

    ```sh
    $ ./tpccbenchmark --load=true --nodes=${IPS} --warehouses=2666  --loaderthreads 20
    ```

1. Run the benchmark using the following command:

    ```sh
    $ ./tpccbenchmark --execute=true --warmup-time-secs=300 --nodes=${IPS} --warehouses=2666 --num-connections=266
    ```

1. Gather the results.

    | Nodes | vCPUs | Warehouses |   TPMC   | Efficiency(%) | Connections | New Order Latency |
    | :---: | :---: | :--------: | :------- | :-----------: | :---------: | :---------------: |
    |   4   |  32   |    1000    | 34212.57 |     99.79     |     266     |     53.92 ms      |

1. Clean up the test run using the following command:

    ```sh
    $ ./tpccbenchmark --clear=true --nodes=${IPS} --warehouses=2000
    ```

## Add the 5th node

<!-- begin: nav tabs -->
{{<nav/tabs list="local,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local numnodes="1" masterip="127.0.0.1" ips="127.0.0.5" dirnum="5" locations="aws.us-east.us-east-1a" alias="no" instructions="no" destroy="no" dataplacement="no" status="no" collapse="Add the 5th node">}}

Add the new IP address to the existing variable.

```bash
IPS=${IPS},127.0.0.5
```

{{</nav/panel>}}
{{<nav/panel name="cloud">}}
Add a node using the [Edit Infrastructure](../../../yugabyte-cloud/cloud-clusters/configure-clusters/) option and increase the node count by 1.
{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

Re-run the test as follows:

1. Initialize the database needed for the benchmark by following the instructions specific to your cluster.

    Set up the TPC-C database schema with the following command:

    ```sh
    $ ./tpccbenchmark --create=true --nodes=${IPS}
    ```

    Populate the database with data needed for the benchmark with the following command:

    ```sh
    $ ./tpccbenchmark --load=true --nodes=${IPS} --warehouses=3333  --loaderthreads 20
    ```

1. Run the benchmark from two clients as follows:

    On client 1, run the following command:

    ```sh
    $ ./tpccbenchmark --execute=true --warmup-time-secs=300 --nodes=${IPS} --warehouses=1500 --start-warehouse-id=1 --total-warehouses=3333 --num-connections=333
    ```

    On client 2, run the following command:

    ```sh
    $ ./tpccbenchmark --execute=true --warmup-time-secs=300 --nodes=${IPS} --warehouses=1833 --start-warehouse-id=1501 --total-warehouses=3333 --num-connections=333
    ```

1. Gather the results.

    | Nodes | vCPUs | Warehouses |   TPMC   | Efficiency(%) | Connections | New Order Latency |
    | :---: | :---: | :--------: | :------- | :-----------: | :---------: | :---------------: |
    |   5   |  40   |    2000    | 42772.6  |     99.79     |     333     |     51.01 ms      |

1. Clean up the test run using the following command:

    ```sh
    $ ./tpccbenchmark --clear=true --nodes=${IPS} --warehouses=2000
    ```

## Add the 6th node

<!-- begin: nav tabs -->
{{<nav/tabs list="local,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local numnodes="1" masterip="127.0.0.1" ips="127.0.0.6" dirnum="6" locations="aws.us-east.us-east-1a" alias="no" instructions="no" destroy="no" dataplacement="no" status="no" collapse="Add the 6th node">}}

Add the new IP address to the existing variable.

```bash
IPS=${IPS},127.0.0.6
```

{{</nav/panel>}}
{{<nav/panel name="cloud">}}
Add a node using the [Edit Infrastructure](../../../yugabyte-cloud/cloud-clusters/configure-clusters/) option and increase the node count by 1.
{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

Re-run the test as follows:

1. Initialize the database needed for the benchmark by following the instructions specific to your cluster.

    Set up the TPC-C database schema with the following command:

    ```sh
    $ ./tpccbenchmark --create=true --nodes=${IPS}
    ```

    Populate the database with data needed for the benchmark with the following command:

    ```sh
    $ ./tpccbenchmark --load=true --nodes=${IPS} --warehouses=4000 --loaderthreads 20
    ```

1. Run the benchmark from two clients as follows:

    On client 1, run the following command:

    ```sh
    $ ./tpccbenchmark --execute=true --warmup-time-secs=300 --nodes=${IPS} --warehouses=2000 --start-warehouse-id=1 --total-warehouses=4000 --num-connections=200
    ```

    On client 2, run the following command:

    ```sh
    $ ./tpccbenchmark --execute=true --warmup-time-secs=300 --nodes=${IPS} --warehouses=2000 --start-warehouse-id=2001 --total-warehouses=4000 --num-connections=200
    ```

1. Gather the results.

    | Nodes | vCPUs | Warehouses |   TPMC   | Efficiency(%) | Connections | New Order Latency |
    | :---: | :---: | :--------: | :------- | :-----------: | :---------: | :---------------: |
    |   6   |  48   |    4000    | 51296.9  |     99.72     |     400     |     62.09 ms      |

1. Clean up the test run using the following command:

    ```sh
    $ ./tpccbenchmark --clear=true --nodes=${IPS} --warehouses=2000
    ```

## Conclusion

With the addition of new nodes, the YugabyteDB cluster can handle more transactions per minute. This linear scalability and high efficiency underscore YugabyteDB's architectural strengths: its ability to distribute workloads evenly across nodes, manage resources optimally, and handle the increased concurrency and data volume that come with cluster growth.
