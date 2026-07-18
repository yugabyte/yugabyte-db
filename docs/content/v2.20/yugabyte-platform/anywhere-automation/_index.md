---
title: Automation in YugabyteDB Anywhere
headerTitle: Automation
linkTitle: Automation
description: Automation tools for YugabyteDB Anywhere.
headcontent: Manage YugabyteDB Anywhere accounts and deployments using automation
menu:
  v2.20_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: anywhere-automation
    weight: 670
type: indexpage
showRightNav: true
---

Use the following automation tools to manage your YugabyteDB Anywhere installation and universes:

| Automation | Description |
| :--------- | :---------- |
| [REST API](anywhere-api/) | Deploy and manage database universes using a REST API. |
| [Terraform provider](anywhere-terraform/) | Provider for automating YugabyteDB Anywhere resources that are accessible via the API. |

## Authentication

For access, automation tools require authentication in the form of an API token. An API token is associated with a particular user account, and gives all the permissions that the user has. For example, if the user is an admin, that user's API token lets you use all of the admin privileged operations.

Generating a new API token invalidates your existing token. Only the most-recently generated API token is valid.

To obtain your API token:

1. In YugabyteDB Anywhere, click the profile icon at the top right and choose **User Profile**.

1. Under **API Key management**, copy your API token. If the **API Token** field is blank, click **Generate Key**, and then copy the resulting API token.

To obtain your API token using the REST API, you can use the Auth token generated when you sign in. (By default, Auth tokens expire after 12 hours.)

Do the following:

1. Sign in to YugabyteDB Anywhere:

    ```sh
    curl --location --request POST 'https://<yba-ip>/api/v1/login' \
      --header 'Content-Type: application/json' \
      --data-raw '{ "email":"myemail@example.com","password":"mypassword" }' \
      -k
    ```

    This returns an Auth token. For example:

    ```yaml{.nocopy}
    {"authToken":"3efcf456aa78d912bfc3bd4ef5dcc678","customerUUID":"1e2c345d-e678-9fb1-b234-fcb56a78c9e0","userUUID":"234ec5e6-b7bd-8e90-1234-5bfe678d90d1"}%
    ```

1. Use the Auth token to generate an API token:

    ```sh
    curl --request PUT \
    --url https://<yba-ip>/api/v1/customers/1e2c345d-e678-9fb1-b234-fcb56a78c9e0/api_token \
    --header 'Accept: application/json' \
    --header 'X-AUTH-TOKEN: 3efcf456aa78d912bfc3bd4ef5dcc678' \
    -k
    ```

    ```yaml{.nocopy}
    {"apiToken":"27011623-05b6-47cc-81a8-bce47ba911a4","apiTokenVersion":6,"customerUUID":"1e2c345d-e678-9fb1-b234-fcb56a78c9e0","userUUID":"234ec5e6-b7bd-8e90-1234-5bfe678d90d1"}%
    ```

## Account details

For some REST API commands, you may need one or more of the following account details.

### Customer ID

To view your customer ID, in YugabyteDB Anywhere, click the **Profile** icon in the top right corner of the YugabyteDB Anywhere window, and choose **User Profile**.

To get your customer UUID using the API, use the following command:

```sh
curl \
  -H "X-AUTH-YW-API-TOKEN: myYBAnywhereApiToken" \
    [http|https]://<yba-ip>/api/v1/customers
```

This returns the UUID. For example:

```yaml{.nocopy}
["1e2c345d-e678-9fb1-b234-fcb56a78c9e0"]
```

### Universe ID

Every universe has a unique ID. To obtain a universe ID in YugabyteDB Anywhere, click **Universes** in the left column, then click the name of the universe. The URL of the universe's **Overview** page ends with the universe's UUID. For example:

```output
https://myPlatformServer/universes/d73833fc-0812-4a01-98f8-f4f24db76dbe
```
