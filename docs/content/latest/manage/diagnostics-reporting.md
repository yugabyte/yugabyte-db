---
title: Diagnostics Reporting
linkTitle: Diagnostics Reporting
description: Diagnostics Reporting
aliases:
  - /manage/diagnostics-reporting/
menu:
  latest:
    identifier: diagnostics-reporting
    parent: manage
    weight: 704
---

[yb-master](../../admin/yb-master/) and [yb-tserver](../../admin/yb-tserver/) binaries report cluster diagnostics to YugaByte's diagnostics service every time a new cluster gets created and every hour thereafter. User data stored in YugaByte DB as well as any personally identifiable information regarding the user is never collected or reported.

## Data collected

The data collected depends on the collection level set. See the [section](#configuration-flags) below on how to change the collection level.

### Collection level: low [Default]

```sh
Collection time
Cluster uuid
Node uuid
Node type (tserver or master)
Number of masters (yb-master processes)
Number of tablet servers (yb-tserver processes)
Number of tables
Number of tablets
Cluster configuration (gflags)
```

### Collection level: medium

```sh
Everything we collect for level “low” plus:
Performance metrics
RPC metrics
```


### Collection level: high
Same as `medium` level. In other words, no extra information is collected for this level. It will be used in the future to collect additional dianostics information such as error logs.

#### Example of metrics collected

```sh
[
    {
        "type": "tablet",
        "id": "eca8b3cfb2ee4eca94dde519634e1e38",
        "attributes": {
            "partition": "hash_split: [16380, 19110)",
            "table_name": "redis",
            "table_id": "82f5f7ab81a44923b5f544fbd0664afe"
        },
        "metrics": [
            {
                "name": "log_reader_bytes_read",
                "value": 0
            },
            {
                "name": "log_reader_entries_read",
                "value": 0
            },
            {
                "name": "log_reader_read_batch_latency",
                "total_count": 0,
                "min": 0,
                "mean": 0,
                "percentile_75": 0,
                "percentile_95": 0,
                "percentile_99": 0,
                "percentile_99_9": 0,
                "percentile_99_99": 0,
                "max": 0,
                "total_sum": 0
            }
         ]
    }
}
```         

#### Example of RPCs being collected

```sh
{
    "inbound_connections": [
        {
            "remote_ip": "10.150.0.20:41134",
            "state": "OPEN",
            "remote_user_credentials": "{real_user=yugabyte, eff_user=}",
            "processed_call_count": 2456
        },
        {
            "remote_ip": "10.150.0.20:54141",
            "state": "OPEN",
            "remote_user_credentials": "{real_user=yugabyte, eff_user=}",
            "processed_call_count": 2471
        }
    ]
}
```

## Configuration flags

You can add the following flags while starting the [yb-master](../../admin/yb-master/) and [yb-tserver](../../admin/yb-tserver/) binaries to configure the diagnostics reporting behavior of YugaByte DB. 

Flag | Default | Description 
----------------------|---------|------------------------
`--callhome_collection_level` |  `low` | Collection level with possible values of `low`, `medium`, or `high`
`--callhome_interval_secs` | 3600 | Collection interval in seconds
`--callhome_url ` | `http://diagnostics.yugabyte.com` | Endpoint where diagnostics information is reported
`--callhome_enabled` | `true` | Controls whether diagnostics information is collected and reported. Set to `false` to disable collection.
