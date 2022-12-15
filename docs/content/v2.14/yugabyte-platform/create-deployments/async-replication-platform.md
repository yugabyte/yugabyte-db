---
title: Deploy to two data centers with xCluster replication
headerTitle: xCluster replication
linkTitle: xCluster replication
description: Enable deployment using asynchronous replication between two data centers
menu:
  v2.14_yugabyte-platform:
    parent: create-deployments
    identifier: async-replication-platform
    weight: 633
type: docs
---

YugabyteDB Anywhere allows you to use its UI or [API](https://api-docs.yugabyte.com/docs/yugabyte-platform/) to manage xCluster replication between independent YugabyteDB clusters. You can perform deployment via unidirectional (master-follower) or [bidirectional](#set-up-bidirectional-replication) (multi-master) xCluster replication between two data centers.

Within the concept of replication, universes are divided into the following categories:

- A source universe contains the original data that is subject to replication.

  Note that in the current release, replicating a source universe that has already been populated with data can be done only by contacting Yugabyte Support.

- A target universe is the recipient of the replicated data. One source universe can replicate to one or more target universes.

For additional information on xCluster replication in YugabyteDB, see the following:

- [xCluster replication: overview and architecture](/preview/architecture/docdb-replication/async-replication/)
- [xCluster replication between universes in YugabyteDB](/preview/deploy/multi-dc/async-replication/)

You can use the YugabyteDB Anywhere UI to set up and configure xCluster replication for universes whose tables do not contain data. In addition, you can perform monitoring by accessing the information about the replication lag and enabling alerts on excessive lag.

## Set up replication

You can set up xCluster replication as follows:

1. Open the YugabyteDB Anywhere UI and navigate to **Universes**.

2. Select the universe you want to replicate and navigate to **Replication**.

3. Click **Configure Replication** to open the dialog shown in the following illustration:<br><br>

   ![Configure Replication](/images/yp/asynch-replication-2.png)<br><br>

4. Provide the name for your replication.

5. Select the target universe.

6. Click **Next: Select Tables**.

7. From a list of common tables between source and target universes, select the tables you want to include in the replication and then click **Create Replication**, as per the following illustration:<br><br>

   ![Create Replication](/images/yp/asynch-replication-3.png)

## Configure replication

You can configure an existing replication as follows:

1. Open the YugabyteDB Anywhere UI and navigate to **Universes**.

2. Select the universe whose existing replication you want to modify and then navigate to **Replication**, as per the following illustration:<br><br>

   ![Replication](/images/yp/asynch-replication-1.png)<br><br>

3. Click **Configure Replication** and perform steps 4 through 7 from [How to set up replication](#set-up-replication).

## View, manage, and monitor replication

To view and manage an existing replication, as well as configure monitoring, click the replication name to open the details page shown in the following illustration:

<br>

![Replication Details](/images/yp/asynch-replication-4.png)

This page allows you to do the following:

- View the replication details.

- View and modify the list of tables included in the replication, as follows:

  - Select **Tables**, as per the following illustration:<br><br>

    ![Tables](/images/yp/asynch-replication-7.png)<br><br>

  - Click **Modify Tables**.

  - Use the **Add tables to the replication** dialog to change the table selection, as per the following illustration:<br><br>

    ![Change Tables](/images/yp/asynch-replication-8.png)<br><br>

    The following illustration shows the **Add tables to the replication** dialog after modifications:<br><br>

    ![Change Tables](/images/yp/asynch-replication-9.png)<br><br>

- Configure the replication, as follows:

  - Click **Actions > Edit replication configuration**.

  - Make changes using the **Edit cluster replication** dialog shown in the following illustration:<br><br>

    ![Edit Replication](/images/yp/asynch-replication-5.png)<br><br>

- Set up monitoring by configuring alerts, as follows:

  - Click **Configure Alert**.

  - Use the **Configure Replication Alert** dialog to enable or disable alert issued when the replication lag exceeds the specified threshold, as per the following illustration:<br><br>

    ![Alert](/images/yp/asynch-replication-6.png)<br><br>

- Pause the replication process (stop the traffic) by clicking **Pause Replication**. This is useful when performing maintenance. Paused replications can be resumed from the last checkpoint.

- Delete the universe replication by clicking **Actions > Delete replication**.

## Set up bidirectional replication

You can set up bidirectional replication using either the YugabyteDB Anywhere UI or API by creating two separate replication configurations. Under this scenario, a source universe of the first replication becomes the target universe of the second replication, and vice versa.

<!--

## Use the REST API

You may choose to use the API to manage universes. You can call the following REST API endpoint on your YugabyteDB Anywhere instance for the source universe and the target universe involved in the xCluster replication between two data sources:

```http
PUT /api/customers/<customerUUID>/universes/<universeUUID>/setup_universe_2dc
```

*customerUUID* represents your customer UUID, and *universeUUID* represents the UUID of the universe (producer or consumer). The request should include an `X-AUTH-YW-API-TOKEN` header with your YugabyteDB Anywhere API key, as shown in the following example `curl` command:

```sh
curl -X PUT \
  -H "X-AUTH-YW-API-TOKEN: myPlatformApiToken" \
https://myPlatformServer/api/customers/customerUUID/universes/universeUUID/setup_universe_2dc
```

You can find your user UUID in YugabyteDB Anywhere as follows:

- Click the person icon at the top right of any YugabyteDB Anywhere page and open **Profile > General**.

- Copy your API token. If the **API Token** field is blank, click **Generate Key**, and then copy the resulting API token. Generating a new API token invalidates your existing token. Only the most-recently generated API token is valid.

- From a command line, issue a `curl` command of the following form:

  ```sh
  curl \
    -H "X-AUTH-YW-API-TOKEN: myPlatformApiToken" \
      [http|https]://myPlatformServer/api/customers
  ```

  <br>For example:

  ```sh
  curl -X "X-AUTH-YW-API-TOKEN: e5c6eb2f-7e30-4d5e-b0a2-f197b01d9f79" \
    http://localhost/api/customers
  ```

- Copy your UUID from the resulting JSON output shown in the following example, omitting the double quotes and square brackets:

  ```
  ["6553ea6d-485c-4ae8-861a-736c2c29ec46"]
  ```

  <br>To find a universe's UUID in YugabyteDB Anywhere, click **Universes** in the left column, then click the name of the universe. The URL of the universe's **Overview** page ends with the universe's UUID. For example, `http://myPlatformServer/universes/d73833fc-0812-4a01-98f8-f4f24db76dbe`

-->
