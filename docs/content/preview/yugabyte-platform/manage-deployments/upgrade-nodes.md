---
title: Apply operating system upgrades and patches to universe nodes
headerTitle: Patch nodes
linkTitle: Patch nodes
description: Apply operating system upgrades and patches to universe nodes.
headcontent: Perform maintenance on universe nodes
menu:
  preview_yugabyte-platform:
    identifier: upgrade-nodes
    parent: manage-deployments
    weight: 20
type: docs
---

If a virtual machine or a physical server in a universe requires operating system (OS) updates or patches, you need to pause node processes before applying updates.

## Prerequisites

- Increase the WAL log retention time, as the time between stop and start will most likely be more than 15 minutes. Set the WAL log retention time using the `--log_min_seconds_to_retain` YB-TServer flag. Refer to [Edit configuration flags](../edit-config-flags/).

## Patch nodes

For each node in the universe, use the following general procedure:

1. Make sure that all nodes in the universe are running correctly.

1. Stop the processes for the node to be patched.

    In YugabyteDB Anywhere, navigate to the universe **Nodes** tab and, for the node, click **Actions** and choose **Stop Processes**.

    If using the YBA API, use the following command:

    ```sh
    curl '<platform-url>/api/v1/customers/<customer_uuid>/universes/<universe_uuid>/nodes/<node_name>' -X 'PUT' -H 'X-AUTH-YW-API-TOKEN: <api-token>' -H 'Content-Type: application/json' -H 'Accept: application/json, text/plain, */*' \
    --data-raw '{"nodeAction":"STOP"}'
    ```

1. Perform the steps to update or patch the Linux OS.

    Ensure that the node retains its IP addresses after the inline patching of the Linux OS. Also ensure that the existing data volumes on the node remain untouched by the OS patching mechanism.

1. Start the processes for the node.

    In YugabyteDB Anywhere, navigate to the universe **Nodes** tab and, for the node, click **Actions** and choose **Start Processes**.

    If using the YBA API, use the following command:

    ```shell
    curl '<platform-url>/api/v1/customers/<customer_uuid>/universes/<universe_uuid>/nodes/<node_name>' -X 'PUT' -H 'X-AUTH-YW-API-TOKEN: <api-token>' -H 'Content-Type: application/json' -H 'Accept: application/json, text/plain, */*' \
    --data-raw '{"nodeAction":"START"}'
    ```

1. Make sure all nodes in the universe are running correctly.
