---
title: Deploy to two data centers with asynchronous replication
headerTitle: Asynchronous Replication
linkTitle: Asynchronous replication
description: Enable deployment using asynchronous replication between two data centers.
menu:
  v2.6:
    parent: create-deployments
    identifier: async-replication-platform
    weight: 633
type: page
isTocNested: true
showAsideToc: false
---

You can perform deployment via unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between data centers.

If you are using Yugabyte Platform to manage universes, you need to call the following REST API endpoint on your Yugabyte Platform instance for the producer universe and the consumer universe involved in the asynchronous replication between two data sources:

```sh
PUT /api/customers/<customerUUID>/universes/<universeUUID>/setup_universe_2dc
```

*customerUUID* represents your customer UUID, and *universeUUID* represents the UUID of the universe (producer or consumer). The request should include an `X-AUTH-YW-API-TOKEN` header with your Yugabyte Platform API key, as shown in the following example `curl` command:

```sh
curl -X PUT \
  -H "X-AUTH-YW-API-TOKEN: myPlatformApiToken" \
https://myPlatformServer/api/customers/customerUUID/universes/universeUUID/setup_universe_2dc
```

You can find your customer UUID in Yugabyte Platform as follows:

- Click the person icon at the top left of any Yugabyte Platform page and open **Profile > General**.

- Copy your API token. If the **API Token** field is blank, click **Generate Key**, and then copy the resulting API token. Generating a new API token invalidates your existing token. Only the most-recently generated API token is valid.

- From a command line, issue a `curl` command of the following form:

  ```sh
  curl \
    -H "X-AUTH-YW-API-TOKEN: myPlatformApiToken" \
      [http|https]://myPlatformServer/api/customers
  ```

  For example:

  ```sh
  curl -X "X-AUTH-YW-API-TOKEN: e5c6eb2f-7e30-4d5e-b0a2-f197b01d9f79" \
    http://localhost/api/customers
  ```

- Copy your UUID from the resulting JSON output shown in the following example, only without the double quotes and square brackets:

  ```
  ["6553ea6d-485c-4ae8-861a-736c2c29ec46"]
  ```

  To find a universe's UUID in Yugabyte Platform, click **Universes** in the left column, then click the name of the universe. The URL of the universe's **Overview** page ends with the universe's UUID. For example, `http://myPlatformServer/universes/d73833fc-0812-4a01-98f8-f4f24db76dbe`

