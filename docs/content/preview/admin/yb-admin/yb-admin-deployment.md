---
title: yb-admin - Deployment commands
headerTitle: yb-admin Deployment commands
linkTitle: Deployment commands
description: yb-admin Deployment commands.
menu:
  preview:
    identifier: yb-admin-deployment
    parent: yb-admin
    weight: 40
type: docs
---

## Multi-zone and multi-region deployment commands

### modify_placement_info

Modifies the placement information (cloud, region, and zone) for a deployment.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    modify_placement_info <placement_info> <replication_factor> \
    [ <placement_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *placement_info*: Comma-delimited list of placements for *cloud*.*region*.*zone*. Optionally, after each placement block, we can also specify a minimum replica count separated by a colon. This count indicates how many minimum replicas of each tablet we want in that placement block. Its default value is 1. It is not recommended to repeat the same placement multiple times but instead specify the total count after the colon. However, in the event that the user specifies a placement multiple times, the total count from all mentions is taken.
* *replication_factor*: The number of replicas for each tablet. This value should be greater than or equal to the total of replica counts specified in *placement_info*.
* *placement_id*: The identifier of the primary cluster, which can be any unique string. Optional. If not set, a randomly-generated ID will be used.

**Example**

```sh
./bin/yb-admin \
    -master_addresses $MASTER_RPC_ADDRS \
    modify_placement_info  \
    aws.us-west.us-west-2a:2,aws.us-west.us-west-2b:2,aws.us-west.us-west-2c 5
```

This will place a minimum of:

1. 2 replicas in aws.us-west.us-west-2a
2. 2 replicas in aws.us-west.us-west-2b
3. 1 replica in aws.us-west.us-west-2c

You can verify the new placement information by running the following `curl` command:

```sh
curl -s http://<any-master-ip>:7000/cluster-config
```

### set_preferred_zones

Sets the preferred availability zones (AZs) and regions. Tablet leaders are placed in alive and healthy nodes of AZs in order of preference. When no healthy node is available in the most preferred AZs (preference value 1), then alive and healthy nodes from the next preferred AZs are picked. AZs with no preference are equally eligible to host tablet leaders.

Having all tablet leaders reside in a single region reduces the number of network hops for the database to write transactions, which increases performance and reduces latency.

{{< note title="Note" >}}

* Make sure you've already run [`modify_placement_info`](#modify-placement-info) command beforehand.

* By default, the transaction status tablet leaders don't respect these preferred zones and are balanced across all nodes. Transactions include a roundtrip from the user to the transaction status tablet serving the transaction - using the leader closest to the user rather than forcing a roundtrip to the preferred zone improves performance.

* Leader blacklisted nodes don't host any leaders irrespective of their preference.

* Cluster configuration stores preferred zones in either affinitized_leaders or multi_affinitized_leaders object.

* Tablespaces don't inherit cluster-level placement information, leader preference, or read replica configurations.

* If the client application uses a smart driver, set the [topology keys](../../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing) to target the preferred zones.

{{< /note >}}

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    set_preferred_zones <cloud.region.zone>[:preference] \
    [<cloud.region.zone>[:preference]]...
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *cloud.region.zone*: Specifies the cloud, region, and zone. Default value is `cloud1.datacenter1.rack1`.
* *preference*: Specifies the leader preference for a zone. Values have to be contiguous non-zero integers. Multiple zones can have the same value. Default value is 1.

**Example**

Suppose you have a deployment in the following regions: `gcp.us-west1.us-west1-a`, `gcp.us-west1.us-west1-b`, `gcp.asia-northeast1.asia-northeast1-a`, and `gcp.us-east4.us-east4-a`. Looking at the cluster configuration:

```sh
curl -s http://<any-master-ip>:7000/cluster-config
```

Here is a sample configuration:

```output
replication_info {
  live_replicas {
    num_replicas: 5
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-west1"
        placement_zone: "us-west1-a"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-west1"
        placement_zone: "us-west1-b"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-east4"
        placement_zone: "us-east4-a"
      }
      min_num_replicas: 2
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-asia-northeast1"
        placement_zone: "us-asia-northeast1-a"
      }
      min_num_replicas: 1
    }
  }
}
```

The following command sets the preferred region to `gcp.us-west1` and the fallback to zone `gcp.us-east4.us-east4-a`:

```sh
ssh -i $PEM $ADMIN_USER@$MASTER1 \
   ~/master/bin/yb-admin --master_addresses $MASTER_RPC_ADDRS \
    set_preferred_zones \
    gcp.us-west1.us-west1-a:1 \
    gcp.us-west1.us-west1-b:1 \
    gcp.us-east4.us-east4-a:2
```

Verify by running the following.

```sh
curl -s http://<any-master-ip>:7000/cluster-config
```

Looking again at the cluster configuration you should see `multi_affinitized_leaders` added:

```output
replication_info {
  live_replicas {
    num_replicas: 5
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-west1"
        placement_zone: "us-west1-a"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-west1"
        placement_zone: "us-west1-b"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-east4"
        placement_zone: "us-east4-a"
      }
      min_num_replicas: 2
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-asia-northeast1"
        placement_zone: "us-asia-northeast1-a"
      }
      min_num_replicas: 1
    }
  }
  multi_affinitized_leaders {
    zones {
      placement_cloud: "gcp"
      placement_region: "us-west1"
      placement_zone: "us-west1-a"
    }
    zones {
      placement_cloud: "gcp"
      placement_region: "us-west1"
      placement_zone: "us-west1-b"
    }
  }
  multi_affinitized_leaders {
    zones {
      placement_cloud: "gcp"
      placement_region: "us-east4"
      placement_zone: "us-east4-a"
    }
  }
}
```

## Read replica deployment commands

### add_read_replica_placement_info

Add a read replica cluster to the master configuration.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    add_read_replica_placement_info <placement_info> \
    <replication_factor> \
    [ <placement_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *placement_info*: A comma-delimited list of placements for *cloud*.*region*.*zone*. Default value is `cloud1.datacenter1.rack1`.
* *replication_factor*: The number of replicas.
* *placement_id*: The identifier of the read replica cluster, which can be any unique string. If not set, a randomly-generated ID will be used. Primary and read replica clusters must use different placement IDs.

### modify_read_replica_placement_info

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    modify_read_replica_placement_info <placement_info> \
    <replication_factor> \
    [ <placement_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *placement_info*: A comma-delimited list of placements for *cloud*.*region*.*zone*. Default value is `cloud1.datacenter1.rack1`.
* *replication_factor*: The number of replicas.
* *placement_id*: The identifier of the read replica cluster, which can be any unique string. If not set, a randomly-generated ID will be used. Primary and read replica clusters must use different placement IDs.

### delete_read_replica_placement_info

Delete the read replica.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    delete_read_replica_placement_info [ <placement_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *placement_id*: The identifier of the read replica cluster, which can be any unique string. If not set, a randomly-generated ID will be used. Primary and read replica clusters must use different placement IDs.
