---
title: MCP servers
headerTitle: MCP servers
linkTitle: MCP
description: YugabyteDB MCP servers, how AI clients connect, and how to handle read-only tools, write tools, and credentials
headcontent: YugabyteDB MCP servers and skills
menu:
  stable:
    identifier: reference-mcp
    parent: reference
    weight: 1000
type: indexpage
showRightNav: true
unversioned: true
---

The Model Context Protocol (MCP) is an industry standard that lets an AI client discover and call tools. While the client is answering, it can call a tool to search documentation, run a query, or change a resource, and then use the result.

## Servers

YugabyteDB publishes the following servers.

| Server | Purpose | Audience | Link |
| :----- | :------ | :------- | :--- |
| YugabyteDB MCP Server | Query a database. Summarize a schema, run read-only SQL, and optionally run guarded writes. | Developers with a YugabyteDB or PostgreSQL database. | [YugabyteDB MCP Server](/stable/develop/ai/mcp-server/) |
| AMP MCP Server | Manage Serverless Postgres clusters, branches, and endpoints in YugabyteDB Aeon. | Aeon users managing Serverless Postgres from an AI client. | [AMP MCP Server](/stable/yugabyte-cloud/serverless/mcp-server-amp/) |
| YugabyteDB Docs MCP Server | Look up official documentation, API and CLI references, blogs, and support articles. | Anyone answering questions about YugabyteDB from an IDE or AI tool. | [Docs MCP Server](docs-mcp-server/) |

## Skills

[YugabyteDB Agent Skills](https://github.com/yugabyte/yugabytedb-skills/blob/main/README.md) provide guidance for clients working with YugabyteDB, including schema design, queries, RAG, performance checks, and the Aeon and Anywhere APIs. The [yugabytedb-skills](https://github.com/yugabyte/yugabytedb-skills) repository lists the skills and provides installation instructions.

To install all skills at once, run the following command:

```sh
npx skills add yugabyte/yugabytedb-skills
```

## How clients connect

A client connects to a server in one of two ways:

- **Remote HTTP.** The server is hosted at a URL. You register that URL. Some servers also require a header, such as an API key. The [Docs MCP Server](docs-mcp-server/) and the [AMP MCP Server](/stable/yugabyte-cloud/serverless/mcp-server-amp/) use this method.
- **Local command.** The client starts a program on your computer, and that program connects to your database. The [YugabyteDB MCP Server](/stable/develop/ai/mcp-server/) uses this method.

For commands, configuration, and sign-in instructions, see the individual [server documentation](#servers). Common clients include Claude Code, Claude Desktop, Cursor, Visual Studio Code, Windsurf, and ChatGPT. After you add a server, restart the client or reconnect it.

## Query a Serverless Postgres endpoint

Registering both the YugabyteDB and AMP servers gives the client two separate connections:

- The AMP MCP Server manages clusters, branches, and endpoints. It authenticates with a YugabyteDB Aeon API key.
- The YugabyteDB MCP Server runs SQL. It authenticates with the database connection string defined in `YUGABYTEDB_URL`.

The connection string names one database on one endpoint. The SQL tools don't see the account's other clusters or branches. To query another branch, set the host and port of the connection string to that branch's endpoint.

A new cluster has one read-write endpoint, on the `main` branch. To add branches and endpoints, see [Manage endpoints](/stable/yugabyte-cloud/serverless/manage/#manage-endpoints).

To obtain the connection parameters, navigate to the endpoint and click **Connect**. See [Connect to an endpoint](/stable/yugabyte-cloud/serverless/create-connect/#connect). Use the parameters and add the password from the cluster credentials file, as the MCP server doesn't prompt for it:

```text
host=<HOST_ADDRESS> port=<PORT> user=<DB_USER> password=<DB_PASSWORD> dbname=postgres sslmode=verify-full sslrootcert=<ROOT_CERT_PATH>
```

If **Connect** dialog includes `sslmode` and `sslrootcert` parameters, replace `<ROOT_CERT_PATH>` with the path to the CA certificate you download in the **Connect** dialog. Replace `postgres` if you're connecting to a different database.

The YugabyteDB MCP Server runs on your computer and connects to the endpoint; you must add your computer's IP address to the endpoint's **Trusted IP addresses**. Set those addresses when you [add the endpoint](/stable/yugabyte-cloud/serverless/manage/#add-a-compute-endpoint).

## Security

### Read-only and write tools

Read-only tools fetch information. A schema summary, a `SELECT`, a search of the docs, and a list of clusters don't change your data.

Write tools create, update, or delete. The client shows you the request and waits for you to confirm it before anything changes. On the YugabyteDB MCP Server, write queries stay off until you enable them. On the AMP MCP Server, a create, update, or delete waits for your confirmation. The Docs MCP Server has no write tools.

Give the credential only the access the task needs. A read-only database role can run the read tools. An Aeon API key with only Read permission can list resources and can't change them.

The client chooses the call and interprets the result. Confirm a change before you accept it, and check the result in the database or in YugabyteDB Aeon.

### Credentials

A connection string, database password, or API key in a client configuration is a secret.

- Keep it out of any file you commit, including a project `.mcp.json` or `.cursor/mcp.json`.
- Some clients store the value in plain text. Claude Code writes request headers to `~/.claude.json`.
- When a key expires, or you think it has leaked, revoke it, create a new one, and update the client configuration.

Each server uses a different credential:

- The Docs MCP Server uses a Google sign-in.
- The AMP MCP Server uses a YugabyteDB Aeon API key from the Serverless account.
- The YugabyteDB MCP Server uses a database connection string.
