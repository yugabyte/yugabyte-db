---
title: YugabyteDB MCP server
headerTitle: YugabyteDB MCP Server
linkTitle: YugabyteDB MCP Server
description: Get LLMs to interact directly with YugabyteDB using a MCP server
headcontent: Get LLMs to interact directly with YugabyteDB
menu:
  stable_develop:
    identifier: tutorials-mcp-server
    parent: tutorials-ai-agentic
    weight: 20
type: docs
---

The YugabyteDB MCP Server is a lightweight, Python-based server that enables LLMs such as Anthropic's Claude to securely connect to and query YugabyteDB. It supports the Model Context Protocol (MCP), an industry standard that allows AI tools to discover and use structured services like databases, file systems, or APIs.

With the YugabyteDB MCP Server, developers can:

- Explore YugabyteDB data using natural language prompts
- Generate AI-powered visualizations from query results
- Enable LLMs to safely issue read-only SQL queries (and optionally guarded writes)
- Map authenticated users to database roles via OIDC identity mapping
- Integrate instantly with tools like Claude Desktop, Cursor, and Windsurf

Learn more about the YugabyteDB MCP Server:

- [Introducing the YugabyteDB MCP Server](https://www.yugabyte.com/blog/yugabytedb-mcp-server/)
- [Unlock AI-Driven Data Experiences with YugabyteDB MCP Server](https://www.yugabyte.com/blog/yugabytedb-mcp-server-on-aws-marketplace/)
- [YugabyteDB Joins Google's MCP Toolbox for AI Agent Development](https://www.yugabyte.com/blog/yugabytedb-joins-googles-mcp-toolbox/)
- [Build a Retail Agent with MCP Toolbox, YugabyteDB, and Google ADK](https://www.yugabyte.com/blog/build-a-retail-agent/)
- [How to Integrate the YugabyteDB MCP Server with Visual Studio Code](https://www.yugabyte.com/blog/integrate-yugabytedb-mcp-server-with-vs-code/)

This tutorial walks you through using the YugabyteDB MCP Server to allow an AI application to access, query, analyze, and interpret data in your YugabyteDB database, using only natural language prompts.

The tutorial uses a YugabyteDB cluster running the [Northwind dataset](/stable/develop/sample-data/northwind/). You connect [Claude](https://claude.com/product/overview) to this database using MCP, and then explore it using natural language prompts.

## Prerequisites

- YugabyteDB {{<release "2025.2">}} or later
- Python 3.10+
- [uv](https://docs.astral.sh/uv/) (recommended) or pip / pipx
- [Claude Desktop](https://claude.ai/download)

## MCP tools

The server exposes three tools that MCP clients can call:

| Tool | Description |
| ---- | ----------- |
| `summarize_database` | Lists tables in a schema (default `public`) with columns and row counts. Read-only. |
| `run_read_only_query` | Executes a SQL query inside `BEGIN READ ONLY` and returns rows as JSON. Read-only. |
| `run_write_query` | Runs INSERT/UPDATE/DELETE/MERGE/TRUNCATE/DDL behind a guardrail blocklist. Destructive; **disabled by default**. Enable with `--enable-write-query` or `YB_MCP_ENABLE_WRITE_QUERY=true`. |

All three tools accept an optional `requested_role` parameter. When OIDC is enabled and the caller's identity claim resolves to multiple mapped database roles (for example, Cognito groups or Keycloak realm roles), the agent can pass `requested_role` to pick one. The server clamps the choice to the JWT's mapped candidates — the agent cannot SET ROLE to a role that is not in the token.

Claude Desktop surfaces read-only badges on the read tools and a confirmation prompt before each `run_write_query` call (`destructiveHint: true`).

## Set up YugabyteDB MCP Server

Install and run the server using one of the following options.

**Option 1 — `uvx` (no install):**

```sh
uvx yugabytedb-mcp-server --help
```

**Option 2 — install the package:**

```sh
pipx install yugabytedb-mcp-server
# or: uv tool install yugabytedb-mcp-server
# or: pip install yugabytedb-mcp-server

yugabytedb-mcp --help
```

**Option 3 — from source (development):**

```sh
git clone https://github.com/yugabyte/yugabytedb-mcp-server.git
cd yugabytedb-mcp-server
uv sync
uv run yugabytedb-mcp --help
```

{{< note title="Note" >}}
There is no longer a `src/server.py` entry point. Always invoke the server via the `yugabytedb-mcp` (or `yugabytedb-mcp-server`) console script.
{{< /note >}}

## Set up YugabyteDB

1. [Download and install](https://download.yugabyte.com) YugabyteDB {{<release "2025.2">}} or later.

1. Start a single-node cluster using [yugabyted](../../../reference/configuration/yugabyted/).

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.1
    ```

1. [Install the Northwind sample database](/stable/develop/sample-data/northwind/#install-the-northwind-sample-database).

## Connect Claude to the server

1. In Claude Desktop, navigate to **Settings > Developer > Edit Config**.

1. Add a new mcpServer entry in the `claude_desktop_config.json`.

    **Via `uvx` (no install):**

    ```json
    {
        "mcpServers": {
            "yugabytedb-mcp": {
                "command": "uvx",
                "args": [
                    "yugabytedb-mcp-server"
                ],
                "env": {
                    "YUGABYTEDB_URL": "dbname=northwind host=localhost port=5433 user=yugabyte password=yugabyte"
                }
            }
        }
    }
    ```

    **Via an installed script** (after `pipx install` or `uv tool install`):

    ```json
    {
        "mcpServers": {
            "yugabytedb-mcp": {
                "command": "yugabytedb-mcp",
                "env": {
                    "YUGABYTEDB_URL": "dbname=northwind host=localhost port=5433 user=yugabyte password=yugabyte"
                }
            }
        }
    }
    ```

    Replace the database user and password as appropriate.

    Config file locations:

    - macOS: `~/Library/Application Support/Claude/claude_desktop_config.json`
    - Windows: `%APPDATA%\Claude\claude_desktop_config.json`

1. Restart Claude to apply changes.

The same configuration works with **Cursor** (Settings → MCP) and **Windsurf** (Settings → Cascade → MCP Servers).

## Prompt 1: Summarize the database

Prompt:

```text
Summarize the database you are connected to.
```

Claude does the following:

- Calls `summarize_database`.
- Lists all tables with schema and row counts.
- Describes what it sees in plain English.

Example output:

```output
This appears to be the classic Northwind database, a sample database
commonly used for demos and tutorials. Here's a summary:

Database Overview
This is a typical e-commerce/trading company database with 14 tables
and approximately 3,400 total rows of data.

Core Business Tables:

- orders (830 rows) - Customer orders with shipping details
- order_details (2,155 rows) - Line items for each order with pricing and quantities
- products (77 rows) - Product catalog with pricing and inventory
- customers (91 rows) - Customer information and contact details
- categories (8 rows) - Product categories

Supporting Tables:

- employees (9 rows) - Employee information including hierarchy (reports_to)
- suppliers (29 rows) - Product suppliers
- shippers (6 rows) - Shipping companies
- territories (53 rows) - Sales territories
- region (4 rows) - Geographic regions
- us_states (51 rows) - US state reference data

Junction Tables:

- employee_territories (49 rows) - Maps employees to territories
- customer_demographics (0 rows) - Empty demographic data
- customer_customer_demo (0 rows) - Empty customer-demographic mapping

This database supports typical business operations like order management,
inventory tracking, employee territories, and supplier relationships. Would
you like me to run any specific queries or analysis on this data?
```

## Prompt 2: Build a dashboard

Prompt:

```text
Build a dashboard with 3 visualizations:

- Monthly sales trend
- Top 10 customers by revenue
- Sales grouped by customer country
```

Claude does the following:

- Calls `run_read_only_query` with safe, read-only SQL.
- Aggregates and structures the results.
- Creates an interactive dashboard for the results.

All with no SQL required.

Example output:

```output
I've created an interactive sales dashboard with all three visualizations you requested:

1. Monthly Sales Trend - A line chart showing revenue progression from July 1996 to
May 1998. You can see a clear upward trend with revenue growing from ~$27k to peaks
of ~$123k per month.
2. Top 10 Customers by Revenue - A horizontal bar chart showing QUICK-Stop, Ernst
Handel, and Save-a-lot Markets as the top three customers, each generating over $100k
in revenue.
3. Sales by Country - A scrollable horizontal bar chart displaying all 21 countries,
with USA and Germany leading at $245k and $230k respectively.

The dashboard features:

- Interactive tooltips showing exact values when you hover over data points
- Clean, modern design with a professional color scheme
- Responsive layout that adapts to different screen sizes
- Currency formatting for easy readability

All data is pulled directly from your Northwind database and shows real sales patterns
over the ~2-year period covered by the dataset.
```

## OIDC and per-user database roles

For shared or remote deployments (HTTP transport), you can enable OIDC (AWS Cognito or a generic OIDC provider) so each authenticated caller runs SQL under their own YugabyteDB role via `SET ROLE`.

Identity mapping mirrors YSQL's native OIDC → role mapping (`ysql_ident_conf_csv` / `matching_claim_key`):

| Environment variable | Purpose |
| -------------------- | ------- |
| `YB_MCP_IDENTITY_CLAIM` | JWT claim used as the identity (default `email`). Supports dotted paths (for example, `realm_access.roles`) and Cognito-style keys (`cognito:groups`). |
| `YB_MCP_IDENTITY_TRANSFORM` | `none` or `strip_domain` when no map file is set. |
| `YB_MCP_IDENTITY_MAP` | Path to a `pg_ident.conf`-style map file (the allowlist of claim value → DB role). |
| `YB_MCP_IDENTITY_MAP_NAME` | Named map inside the file to apply (default `default`). |
| `YB_MCP_REQUIRE_ACCESS_TOKEN` | Cognito-only. When `true`, reject tokens with `token_use` other than `access`. Default `false` for backward compatibility. |

When `YB_MCP_IDENTITY_MAP` is set, unmapped claim values are rejected (`IdentityError`) — the map is the allowlist. JWT audience validation is always on for Cognito and OIDC.

For full provider setup, map file format, worked examples (Cognito, Keycloak, Azure AD), and a migration checklist, see [OIDC.md](https://github.com/yugabyte/yugabytedb-mcp-server/blob/main/OIDC.md) in the MCP server repository.

## Read more

- [YugabyteDB MCP Server repository](https://github.com/yugabyte/yugabytedb-mcp-server)
- [OIDC authentication and identity mapping](https://github.com/yugabyte/yugabytedb-mcp-server/blob/main/OIDC.md)
- [Unlock AI-Driven Data Experiences with YugabyteDB MCP Server on AWS Marketplace](https://www.yugabyte.com/blog/yugabytedb-mcp-server-on-aws-marketplace/)
- [Architecting GenAI and RAG Apps with YugabyteDB](https://www.yugabyte.com/ai/)
