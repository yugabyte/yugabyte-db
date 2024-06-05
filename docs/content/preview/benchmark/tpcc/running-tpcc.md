---
title: Running the TPC-C performance benchmark
headerTitle: Run the TPC-C performance benchmark
linkTitle: Run benchmark
headcontent: Detailed steps to run the TPCC benchmark
menu:
  preview:
    identifier: tpcc-ysql
    parent: tpcc
    weight: 100
type: docs
rightNav:
  hideH3: true
---

To run the TPC-C benchmark with an efficiency of around 99.7%, choose a cluster specification (size and instance type) depending on your estimated IOPS, estimated data size, and expected latency.

You can use the following table for reference.

|   Cluster   | Data set size (GB) | DB IOPS  | Recommended workload (warehouses) | <br>tpmC | Results<br>efficiency | <br>Latency |
| ----------- | ------------------ | -------- | --------------------------------- | -------------- | -------------------- | ----------------- |
| 3 x 2vCPUs  | 39.90              | 1875.77  | 500                               | 6415.7         | 99.78                | 64.08             |
| 3 x 4vCPUs  | 75.46              | 3736.53  | 1000                              | 12829.93       | 99.77                | 73.97             |
| 3 x 8vCPUs  | 141.40             | 7488.01  | 2000                              | 25646.4        | 99.71                | 54.21             |
| 3&nbsp;x&nbsp;16vCPUs | 272.24             | 14987.79 | 4000                              | 51343.5        | 99.81                | 39.46             |

## Instance types

The following table lists the recommended instance types on different cloud providers based on vCPU count.

| vCPU |     AWS     |       AZURE       |            GCP             |
| ---- | ----------- | ----------------- | -------------------------- |
| 2    | m6i.large   | Standard_D2ds_v5  | n2-standard-2              |
| 4    | m6i.xlarge  | Standard_D4ds_v5  | n2-standard-4              |
| 8    | m6i.2xlarge | Standard_D8ds_v5  | n2-standard-8              |
| 16   | m6i.4xlarge | Standard_D16ds_v5 | n2&#8209;standard&#8209;16 |

After you have decided the instance type of the cluster that you need, use the following instructions to run the TPC-C workload.

## Get TPC-C binaries

First, you need the benchmark binaries. To download the TPC-C binaries, run the following commands.

```sh
$ wget https://github.com/yugabyte/tpcc/releases/latest/download/tpcc.tar.gz
$ tar -zxvf tpcc.tar.gz
$ cd tpcc
```

## Client machine

The client machine is where the benchmark is run from. An 8vCPU machine with at least 16GB memory is recommended. The following instance types are recommended for the client machine.

| vCPU |    AWS     |      AZURE      |     GCP      |
| ---- | ---------- | --------------- | ------------ |
| 8    | c5.2xlarge | Standard_F8s_v2 | n2-highcpu-8 |

## Cluster setup

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

{{<warning title="Set up connection options correctly">}}

- When running the client machine outside the VPC, enable Public Access for the cluster. Also, make sure your client's IP is added to the **IP allow list**.
- Add the username (admin) and password from the credential file that you downloaded when you set up your cluster to the [workload_all.xml](#configure-connection-parameters) file.
- Make sure you add the full path of the location of the SSL certificate of your cluster in the `sslCert` tag in the [workload_all.xml](#configure-connection-parameters) file.
- If you are planning to do a horizontal scale test, set the fault tolerance level to **None** so that you can add a single node to the cluster.
{{</warning>}}

Store the IP addresses/public address of the cluster in a shell variable for use in further commands.

```bash
IPS=<cluster-name/IP>
```

{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

## Configure connection parameters

If needed, options like username, password, port, and so on, can be set up using the configuration file `config/workload_all.xml`.

```xml
<port>5433</port>
<username>yugabyte</username>
<password>***</password>
<sslCert>/Users/johnwick/.credentials/root.crt</sslCert>
```

## Initialize the data

Initialize the database needed for the benchmark by following the instructions specific to your cluster.

{{<tabpane text=true >}}

{{% tab header="3 x 2vCPUs" lang="2vcpu" %}}

Set up the TPC-C database schema with the following command:

```sh
$ ./tpccbenchmark --create=true --nodes=${IPS}
```

Populate the database with data needed for the benchmark with the following command:

```sh
$ ./tpccbenchmark --load=true --nodes=${IPS} --warehouses=500 --loaderthreads 48
```

{{% /tab %}}

{{% tab header="3 x 4vCPUs" lang="4vcpu" %}}

Set up the TPC-C database schema with the following command:

```sh
$ ./tpccbenchmark --create=true --nodes=${IPS}
```

Populate the database with data needed for the benchmark with the following command:

```sh
$ ./tpccbenchmark --load=true --nodes=${IPS} --warehouses=1000 --loaderthreads 48
```

{{% /tab %}}

{{% tab header="3 x 8vCPUs" lang="8vcpu" %}}

Set up the TPC-C database schema with the following command:

```sh
$ ./tpccbenchmark --create=true --nodes=${IPS}
```

Populate the database with data needed for the benchmark with the following command:

```sh
$ ./tpccbenchmark --load=true --nodes=${IPS} --warehouses=2000 --loaderthreads 48
```

{{% /tab %}}

{{% tab header="3 x 16vCPUs" lang="16vcpu" %}}

Set up the TPC-C database schema with the following command:

```sh
$ ./tpccbenchmark --create=true --nodes=${IPS}
```

To populate the database with data needed for the benchmark, use two client machines. Run the following load commands on each of the machines respectively.

| Client | Command |
| -----: | :------ |
| 1  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=1 --total-warehouses=2000 --loaderthreads 48 --initial-delay-secs=0 |
| 2  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=2001 --total-warehouses=2000 --loaderthreads 48 --initial-delay-secs=30 |

{{<note>}}
**initial-delay-secs** has been introduced to avoid a sudden overload on the master to fetch catalogs.
{{</note>}}

{{% /tab %}}

{{</tabpane>}}

## Run the benchmark

Run the benchmark by following instructions specific to your cluster.

{{<tabpane text=true >}}

{{% tab header="3 x 2vCPUs" lang="2vcpu" %}}

```sh
$ ./tpccbenchmark --execute=true --warmup-time-secs=60 --nodes=${IPS} --warehouses=500 --num-connections=50
```

{{% /tab %}}

{{% tab header="3 x 4vCPUs" lang="4vcpu" %}}

```sh
$ ./tpccbenchmark --execute=true --warmup-time-secs=60 --nodes=${IPS} --warehouses=1000 --num-connections=100
```

{{% /tab %}}

{{% tab header="3 x 8vCPUs" lang="8vcpu" %}}

```sh
$ ./tpccbenchmark --execute=true --warmup-time-secs=60 --nodes=${IPS} --warehouses=2000 --num-connections=200
```

{{% /tab %}}

{{% tab header="3 x 16vCPUs" lang="16vcpu" %}}

Run the following commands on each of the client machines respectively.

| Client | Command |
| -----: | :------ |
| 1  | ./tpccbenchmark --execute=true --nodes=$IPS --warehouses=2000 --start-warehouse-id=1    --total-warehouses=4000  --num-connections=200  --warmup-time-secs=300 |
| 2  | ./tpccbenchmark --execute=true --nodes=$IPS --warehouses=2000 --start-warehouse-id=2001 --total-warehouses=4000  --num-connections=200  --warmup-time-secs=270 |

{{<note>}}
As **initial-delay-secs** is specified, to ensure that the tests start together, the time was adjusted in the warmup phase.
{{</note>}}

{{% /tab %}}

{{</tabpane>}}

## Analyze the results

{{<tabpane text=true >}}

{{% tab header="3 x 2vCPUs" lang="2vcpu" %}}

|                    Metric                    |  Value  |
| -------------------------------------------- | :------ |
| Efficiency                                   | 99.78   |
| TPMC                                         | 6415.7  |
| Average NewOrder Latency (ms)                | 64.08   |
| Connection Acquisition NewOrder Latency (ms) | 1.86    |
| Total YSQL Ops/sec                           | 1875.77 |

On a 2vCPU cluster, 6,415 tpmC was achieved with 99.78% efficiency keeping the new order latency around 64ms.

{{% /tab %}}

{{% tab header="3 x 4vCPUs" lang="4vcpu" %}}

|                    Metric                    |  Value   |
| -------------------------------------------- | :------- |
| Efficiency                                   | 99.77    |
| TPMC                                         | 12829.93 |
| Average NewOrder Latency (ms)                | 73.97    |
| Connection Acquisition NewOrder Latency (ms) | 0.66     |
| Total YSQL Ops/sec                           | 3736.53  |

On a 4vCPU cluster, 12,829 tpmC was achieved with 99.77% efficiency keeping the new order latency around 73ms.

{{% /tab %}}

{{% tab header="3 x 8vCPUs" lang="8vcpu" %}}

|                    Metric                    |  Value  |
| -------------------------------------------- | :------ |
| Efficiency                                   | 99.71   |
| TPMC                                         | 25646.4 |
| Average NewOrder Latency (ms)                | 54.21   |
| Connection Acquisition NewOrder Latency (ms) | 0.55    |
| Total YSQL Ops/sec                           | 7488.01 |

On an 8vCPU cluster, 25,646 tpmC was achieved with 99.71% efficiency keeping the new order latency around 54ms.

{{% /tab %}}

{{% tab header="3 x 16vCPUs" lang="16vcpu" %}}

|                    Metric                    |  Value   |
| -------------------------------------------- | :------- |
| Efficiency                                   | 99.81    |
| TPMC                                         | 51343.5  |
| Average NewOrder Latency (ms)                | 39.46    |
| Connection Acquisition NewOrder Latency (ms) | 0.51     |
| Total YSQL Ops/sec                           | 14987.79 |

On a 16vCPU cluster, 51,343 tpmC was achieved with 99.81% efficiency keeping the new order latency around 39ms.

{{% /tab %}}

{{</tabpane>}}

## Conclusion

The TPC-C benchmark provides a rigorous and comprehensive method for evaluating the performance of OLTP systems, focusing on throughput, response time, and data consistency. The results will help you decide if YugabyteDB is the right database for your high-throughput, transaction-heavy applications.
