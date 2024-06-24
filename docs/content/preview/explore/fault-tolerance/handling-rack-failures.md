---
title: Handling rack failures
headerTitle: Handling rack failures
linkTitle: Rack failures
description: Racks can be treated as fault zones
headcontent: Server rack-awareness in YugabyteDB
menu:
  preview:
    identifier: handling-rack-failures
    parent: fault-tolerance
    weight: 20
type: docs
---

Data centers provide secure and reliable storage for large amounts of digital information. They also serve as hubs for network connectivity, providing a high-speed and reliable networking infrastructure to transmit data between servers, users, and other devices. To accomplish this, data centers maintain and manage all their machines, routers, and switches in server racks.

YugabyteDB has been designed from the ground up for high availability and fault tolerance, enabling it to survive failures across fault domains. Fault domains can be nodes, zones, regions, or in this case, racks. To survive an outage of one fault domain/rack, a YugabyteDB cluster just needs to replicate its data across three fault domains - this is known as having a replication factor (RF) of 3. To survive the failure of two fault domains, a cluster would need an RF of 5.

{{<note>}}
In YugabyteDB, a zone is the default fault domain.
{{</note>}}

## Node placement definition

The placement of a node is specified using a three-part naming convention in the form cloud.region.zone. The "cloud" represents the actual cloud provider, like AWS, GCP, or Azure. The region represents the geographic location, like US-East or EU-Central. The zone refers to the availability zone where the node is present, like us-east-1a or eu-central-2b. For on-premises data centers, you would set cloud to the data center name, region to the city, and zone to a rack or a group of racks.

## Racks as virtual zones

For on-premises data centers, racks can be treated as virtual zones. You can map your racks to zones, and YugabyteDB will automatically handle rack failures. For an RF 3 cluster, you need 3 fault domains, which means you will need at least 3 racks. If you have more racks (say 6 racks), you can create virtual zones with 2 racks each.

Consider a scenario where you have a data center (dc1) in New York with 9 machines (prod-node [01-09]) hosted in 3 racks (Rack-A, Rack-B, and Rack-C).  Three machines are stored per rack, as listed in the following table.

|  RACK  |   MACHINE    |     IP      |
| ------ | ------------ | ----------- |
| Rack-A | prod-node-01 | 192.168.0.1 |
| Rack-A | prod-node-02 | 192.168.0.2 |
| Rack-A | prod-node-03 | 192.168.0.3 |
| Rack-B | prod-node-04 | 192.168.0.4 |
| Rack-B | prod-node-05 | 192.168.0.5 |
| Rack-B | prod-node-06 | 192.168.0.6 |
| Rack-C | prod-node-07 | 192.168.0.7 |
| Rack-C | prod-node-08 | 192.168.0.8 |
| Rack-C | prod-node-09 | 192.168.0.9 |

For example, to start machine prod-node-01 (located in Rack-A), you can run the following command:

```bash
yugabyted start --advertise_address=192.168.0.1 --cloud_location=dc1.newyork.rack-a
```

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<collapse title="Setup a local cluster">}}

To simulate a local cluster with 3 racks, you can write a basic bash script:

```bash
server=0
JOIN=""
for rack in a b c ; do
    for num in 1 2 3 ; do
        ((server++))
        if [[ ${server} != 1 ]];
        then
           JOIN="--join=127.0.0.1"
        fi
        yugabyted start ${JOIN} --advertise_address=127.0.0.${server} --cloud_location=dc1.newyork.rack-${rack} --base_dir=${HOME}/var/node{server}
    done
done
```

If you connect to your cluster using ysqlsh and fetch the cluster information, you will get an output similar to the following:

```sql
SELECT host, cloud, region, zone FROM yb_servers() ORDER BY host;
```

```output
     host  | cloud  | region  |  zone
-----------+--------+---------+--------
 127.0.0.1 | dc1    | newyork | rack-a
 127.0.0.2 | dc1    | newyork | rack-a
 127.0.0.3 | dc1    | newyork | rack-a
 127.0.0.4 | dc1    | newyork | rack-b
 127.0.0.5 | dc1    | newyork | rack-b
 127.0.0.6 | dc1    | newyork | rack-b
 127.0.0.7 | dc1    | newyork | rack-c
 127.0.0.8 | dc1    | newyork | rack-c
 127.0.0.9 | dc1    | newyork | rack-c
 ```

{{</collapse>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<note>}}
To configure racks as zones in YugabyteDB Anywhere, set the racks as zones in your [on-prem provider settings](../../../yugabyte-platform/configure-yugabyte-platform/on-premises-provider/#provider-settings)
{{</note>}}
{{</nav/panel>}}
{{<nav/panel name="cloud">}}
{{<warning>}} Currently, you can't configure racks as fault domains in YugabyteDB Aeon {{</warning>}}
{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

By default, the replication factor is 3, and the fault tolerance is configured to zone-level resilience. This ensures that YugabyteDB automatically replicates data and places it across the three racks. Because an RF 3 system can handle the failure of one fault domain, this setup can handle the outage (planned or unplanned) of any one rack.

## Combining multiple racks

If your cluster is deployed across more than the required number of fault domains, consider combining two or more racks into virtual zones. For example, with six racks named rack-[A, B, C, D, E, F], you can group them into the required number of fault domains. For an RF 3 setup, divide the racks into three virtual zones (for example, rack-group-1, rack-group-2, and rack-group-3), placing nodes from rack-A and rack-B into virtual zone rack-group-1, and similarly configuring the placement for the others.
