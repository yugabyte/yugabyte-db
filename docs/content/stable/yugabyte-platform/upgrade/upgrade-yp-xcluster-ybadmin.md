---
title: Synchronize replication after upgrade
headerTitle: Synchronize replication after upgrade
linkTitle: Synchronize replication after upgrade
description: Synchronize replication after upgrade via yb-admin
menu:
  stable_yugabyte-platform:
    identifier: upgrade-yp-xcluster-ybadmin
    parent: upgrade
    weight: 82
type: docs
---

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/multi-region-deployments/asynchronous-replication-ysql/) for your universes was set up via `yb-admin` instead of the UI, you need to synchronize the replication after performing the upgrade.

## Update replication group names

Before synchronizing, you must ensure that the replication group names adhere to the following format:

```output
<source_universe_uuid>_<config_name>
```

To check the names, execute the following command:

```sh
yb-admin get_universe_config | python -m json.tool
```

An output might be similar to the following:

```output
"producerMap": {
            "04dfc615-e2bd-4e56-9a3b-198bceb09e43": {
                        "consumerTableId": "5dc1a75bced7465fa298ab3a6fea970b",
                        "producerTableId": "ec26089a41f24d4080cc84b71c6c3391",
```

`producerMap` shows the existing stream name (`04dfc615-e2bd-4e56-9a3b-198bceb09e43` in the preceding example).

If the names do not match the required format, you need to update the replication group names, as follows:

- Change any replication group names as needed using the following command:

  ```sh
  yb-admin alter_universe_replication <existing_stream_name> rename_id <source_universe_uuid>_<existing_stream_name>
  ```

- Verify that all replication group names are in the correct format using the `get_universe_config` command again:

  ```sh
  yb-admin get_universe_config | python -m json.tool
  ```

## Perform synchronization

You can synchronize the xCluster configuration using the [Sync xcluster config](https://api-docs.yugabyte.com/docs/yugabyte-platform/e19b528a55430-sync-xcluster-config) API.

Consider the following example:

```sh
curl --location --request POST 'http://<IP_address>:9000/api/v1/customers/<universe_UUID>/xcluster_configs/sync?targetUniverseUUID=<target_universe_UUID>' \
--header 'X-AUTH-YW-API-TOKEN: <your_API_token>' \
--data-raw ''
```

The target universe UUID in the URL and an empty data flag at the end are required.
