---
title: AMP MCP Server
headerTitle: AMP MCP Server
linkTitle: AMP MCP Server
description: Connect an AI client to the AMP MCP Server to manage Serverless Postgres clusters in YugabyteDB Aeon.
headcontent: Manage Serverless Postgres clusters from an AI client
menu:
  stable_yugabyte-cloud:
    identifier: aeon-serverless-mcp
    parent: aeon-serverless
    weight: 30
type: docs
---

The AMP MCP Server lets an AI client manage the [Serverless Postgres](../) clusters in your YugabyteDB Aeon organization. After you connect the client, you can ask it to list, create, update, and delete clusters, branches, and endpoints.

To query a database on an endpoint, also connect the [YugabyteDB MCP Server](/stable/develop/ai/mcp-server/) and install [YugabyteDB Agent Skills](https://github.com/yugabyte/yugabytedb-skills). The two servers are separate connections. To learn how to use them together, see [Query a Serverless Postgres endpoint](/stable/reference/mcp/#query-a-serverless-postgres-endpoint).

The server is hosted at `https://cloud.yugabyte.com/mcp` and uses Streamable HTTP. Every request authenticates with a YugabyteDB Aeon [API key](../../managed-automation/managed-apikeys/), sent in an `Authorization: Bearer` header. Register the server under the name `amp`. Clients display it as **AMP MCP Server**.

The server works only on your organization's [Serverless account](../../managed-security/manage-accounts/#serverless-account). A key created in any other account can't call the tools.

For information on MCP servers published by YugabyteDB, see [MCP servers](/stable/reference/mcp/).

## Prerequisites

- A Serverless Postgres cluster, so the Serverless account exists. To create your first cluster, see [Quick start](../../cloud-quickstart/).
- An [API key](../../managed-automation/managed-apikeys/#create-an-api-key) created while you are in the Serverless account.

The key's role needs the **AMP Managed cluster** permissions:

| Permission | What the client can do |
| :--------- | :--------------------- |
| Read | List clusters, tasks, and extensions |
| Create | Create a cluster, branch, or endpoint |
| Update | Update an endpoint |
| Delete | Delete a cluster, branch, or endpoint |

A key with Read only can list resources. Select the permissions on **Security > Access Control > Roles**. See [Manage account roles](../../managed-security/managed-roles/).

YugabyteDB Aeon shows a new key only once. Store it in a password manager or another encrypted store. If you lose it, revoke it and create a new one.

## Connect a client

To add the AMP MCP Server, choose the client you use.

{{< tabpane text=true >}}

{{% tab header="Claude Code" lang="claude" %}}

Register the server, then confirm that it is listed as follows:

```sh
claude mcp add --transport http -s user amp https://cloud.yugabyte.com/mcp --header "Authorization: Bearer <API key>"
```

```sh
claude mcp list
```

Replace `<API key>` with your API key. `-s user` makes the server available in every project. Omit `-s user` to register it for the current project only.

Claude Code stores the header, including the API key, in plain text in `~/.claude.json`.

Restart Claude Code, or run `/mcp` and reconnect.

{{% /tab %}}

{{% tab header="Other clients" lang="mcp" %}}

Any client that supports Streamable HTTP and a custom `Authorization` header can connect. Add the server to the client configuration:

```json
{
  "mcpServers": {
    "amp": {
      "type": "http",
      "url": "https://cloud.yugabyte.com/mcp",
      "headers": {
        "Authorization": "Bearer <API key>"
      }
    }
  }
}
```

Replace `<API key>` with your API key. Many clients use `mcpServers`, as in this example. Use the key your client documents.

For Claude Desktop, add the entry to `claude_desktop_config.json`:

- macOS: `~/Library/Application Support/Claude/claude_desktop_config.json`
- Windows: `%APPDATA%\Claude\claude_desktop_config.json`

Restart Claude Desktop to apply the change.

The same configuration works with **Cursor** (Settings > MCP) and **Windsurf** (Settings > Cascade > MCP Servers).

{{% /tab %}}

{{< /tabpane >}}

{{< warning title="Important" >}}

Don't put the API key in a project file that you commit, such as `.mcp.json` or `.cursor/mcp.json`. Anyone with the key can call the YugabyteDB Aeon API as that key's role until you revoke the key or it expires.

{{< /warning >}}

## Example prompts

After the client connects, ask it to manage [clusters, branches, and endpoints](../manage/).

### List clusters

Prompt:

```text
List the Serverless Postgres clusters in this account.
```

The client lists the clusters in the Serverless account. You can ask it to include each cluster's branches and endpoints, the tasks for the account, or the extensions on a branch.

### Create a branch

Prompt:

```text
Create a branch named preview, forked from main.
```

The client shows you the request, including the cluster and the parent branch. Nothing is created until you confirm. A new branch has no endpoint until you add one.

Creating, updating, or deleting works the same way: the client shows the request first, and nothing changes until you confirm.

## Replace an expired key

When the API key expires, the client can no longer call the server. [Create a new key](../../managed-automation/managed-apikeys/#create-an-api-key) in the Serverless account and replace the key in the client configuration. Revoke the old key if it is still listed.

{{< note title="Important" >}}

Responses from the AI client are generated by AI and may not be fully accurate. After you confirm a change, check the cluster in YugabyteDB Aeon. To see a task's progress, or the error if it failed, click **Tasks** in the left navigation.

{{< /note >}}

## Read more

- [MCP servers](/stable/reference/mcp/), for how clients connect and how to handle credentials
- [YugabyteDB MCP Server](/stable/develop/ai/mcp-server/), to query data in a database
- [Manage a cluster](../manage/), for branches, endpoints, and compute
- [API keys](../../managed-automation/managed-apikeys/), to create or revoke the key this server uses
