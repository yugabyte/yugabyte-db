---
title: Upgrade YugabyteDB Anywhere using Replicated
headerTitle: Upgrade YugabyteDB Anywhere using Replicated
linkTitle: Upgrade using Replicated
description: Use Replicated to upgrade YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: upgrade-yp-replicated
    parent: upgrade
    weight: 80
type: docs
---

You can use [Replicated](https://www.replicated.com/) to upgrade your YugabyteDB Anywhere to a newer version.

## Upgrade using Replicated

To start the upgrade, log in to the Replicated Admin Console via <https://:8800> and then perform the following:

- Navigate to **Dashboard** and click **View release history** to open **Release History**, as shown in the following illustration:

  ![image](/images/yb-platform/upgrade-replicated1.png)

- Find the required release version and click the corresponding **Install**.

If the required version is not in the list, click **Check for updates** to refresh the list of releases available in the channel to which you are subscribed.

If the required release version is in a different channel (for example, you want to upgrade from 2.4.*n* release family to 2.6.*n*), start by updating the channel, as follows:

- Click the gear icon and select **View License**, as per the following illustration:

  ![image](/images/yb-platform/upgrade-replicated2.png)

- In the **License** view, click **change** for **Release Channel**, as per the following illustration:

  ![image](/images/yb-platform/upgrade-replicated3.png)

  Note that if you do not have permissions to access the new release channel, you should contact {{% support-platform %}}.

- Click **Sync License**.

- Navigate back to **Release History**, locate the release you need, and then click the corresponding **Install**.

## Synchronize xCluster replication

If you set up xCluster replication using yb-admin (instead of via the Platform UI) and upgrade Platform to 2.12+, you need to synchronize xCluster replication by running the [Sync xcluster config](https://api-docs.yugabyte.com/docs/yugabyte-platform/e19b528a55430-sync-xcluster-config) API call after upgrading.

Before doing this, you need to ensure that the xCluster replication group names are named using the new format.

### Update replication group names

xCluster replication set up using yb-admin typically doesn't use the correct naming convention for the replication stream, which should match the following:

```output
<source/producer_universe_uuid>_<config_name>
```

To update replication group names, do the following:

1. Check the replication group names using the following command:

    ```sh
    yb-admin get_universe_config | python -m json.tool
    ```

    ```output
    "producerMap": {
                "04dfc615-e2bd-4e56-9a3b-198bceb09e43": {
                            "consumerTableId": "5dc1a75bced7465fa298ab3a6fea970b",
                            "producerTableId": "ec26089a41f24d4080cc84b71c6c3391",
    ```

    `producerMap` shows the existing stream name (`04dfc615-e2bd-4e56-9a3b-198bceb09e43` in the preceding example).

2. Fix any replication group names as needed using the following command:

    ```sh
    yb-admin alter_universe_replication <existing_stream_name> rename_id <producer_universe_uuid>_<existing_stream_name>
    ```

3. Verify all replication group names are in the correct format using the following command:

    ```sh
    yb-admin get_universe_config | python -m json.tool
    ```

### Synchronize the xCluster configuration

To synchronize the xCluster configuration, run the [Sync xcluster config](https://api-docs.yugabyte.com/docs/yugabyte-platform/e19b528a55430-sync-xcluster-config) API command.

Be sure to add the target/consumer UUID in the URL and add an empty data flag. For example:

```sh
curl --location --request POST 'http://<IP_address>:9000/api/v1/customers/<universe_UUID>/xcluster_configs/sync?targetUniverseUUID=<consumer_universe_UUID>' \
--header 'X-AUTH-YW-API-TOKEN: <your_API_token>' \
--data-raw ''
```
