---
title: Automation in YugabyteDB Anywhere
headerTitle: Automation
linkTitle: Automation
description: Automation tools for YugabyteDB Anywhere.
headcontent: Manage YugabyteDB Anywhere accounts and deployments using automation
menu:
  v2025.1_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: anywhere-automation
    weight: 680
type: indexpage
showRightNav: true
---

Use the following automation tools to manage your YugabyteDB Anywhere installation and universes:

| Automation | Description |
| :--------- | :---------- |
| [REST API](anywhere-api/) | Deploy and manage database universes using a REST API. |
| [Terraform provider](anywhere-terraform/) | Provider for automating YugabyteDB Anywhere resources that are accessible via the API. |
| [CLI](anywhere-cli/) | Manage YugabyteDB Anywhere resources from the command line. |
| [YugabyteDB Kubernetes Operator](yb-kubernetes-operator/) | Automate the deployment and management of YugabyteDB clusters in Kubernetes environments. |

## Authentication

For access, automation tools require authentication in the form of an API token. An API token is associated with a particular user account, and gives all the permissions that the user has. For example, if the user is an admin, that user's API token lets you use all of the admin privileged operations.

Generating a new API token invalidates your existing token. Only the most-recently generated API token is valid.

To obtain your API token:

1. In YugabyteDB Anywhere, click the profile icon at the top right and choose **User Profile**.

1. Under **API Key management**, copy your API token. If the **API Token** field is blank, click **Generate Key**, and then copy the resulting API token.

To obtain your API token using the REST API, sign in with `POST /api/v1/api_login`. This returns an API token. It also invalidates any API token already issued for that user.

```sh
curl --request POST \
  --url 'https://<yba-ip>/api/v1/api_login' \
  --header 'Content-Type: application/json' \
  --data-raw '{ "email":"myemail@example.com","password":"mypassword" }' \
  -k
```

For example:

```yaml{.nocopy}
{"apiToken":"4.234ec5e6-b7bd-8e90-1234-5bfe678d90d1.27011623-05b6-47cc-81a8-bce47ba911a4","apiTokenVersion":1,"customerUUID":"1e2c345d-e678-9fb1-b234-fcb56a78c9e0","userUUID":"234ec5e6-b7bd-8e90-1234-5bfe678d90d1"}
```

## Account details

For some REST API commands, you may need one or more of the following account details.

### Customer ID

To view your customer ID, in YugabyteDB Anywhere, click the **Profile** icon in the top right corner of the YugabyteDB Anywhere window, and choose **User Profile**.

To get your customer UUID using the API, use the following command:

```sh
curl --request GET \
  --url 'https://<yba-ip>/api/v1/customers' \
  --header 'Accept: application/json' \
  --header 'X-AUTH-YW-API-TOKEN: myYBAnywhereApiToken' \
  -k
```

This returns a list of customers. The customer UUID is the `uuid` field. For example:

```yaml{.nocopy}
[{"uuid":"1e2c345d-e678-9fb1-b234-fcb56a78c9e0","customerId":1,"code":"admin","name":"example","creationDate":"2022-12-12T13:07:18Z"}]
```

### Universe ID

Every universe has a unique ID. To obtain a universe ID in YugabyteDB Anywhere, click **Universes** in the left column, then click the name of the universe. The URL of the universe's **Overview** page ends with the universe's UUID. For example:

```output
https://myPlatformServer/universes/d73833fc-0812-4a01-98f8-f4f24db76dbe
```
