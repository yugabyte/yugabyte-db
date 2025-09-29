---
title: YugabyteDB MCP server
headerTitle: MCP server
linkTitle: MCP server
description: Get LLMs to interact directly with YugabyteDB
headcontent: Get LLMs to interact directly with YugabyteDB
menu:
  preview_develop:
    identifier: tutorials-mcp-server
    parent: tutorials-ai
    weight: 20
type: docs
---

This tutorial walks you through using the Model Context Protocol (MCP) to allow an AI application to access, query, analyze, and interpret data in your YugabyteDB database, using only natural language prompts.

The tutorial uses a YugabyteDB cluster running the [Northwind dataset](../../../../sample-data/northwind/). You connect Claude to this database using MCP, then explore it using natural language prompts.

## YugabyteDB MCP Server

MCP servers provide a standardized way for external tools, data sources, or services to talk to an AI model. Instead of giving a model static context or manually copy-pasting data, an MCP server acts as a bridge between the model and a system, whether a database, API, filesystem, or application.

- Existing services are encapsulated (or fronted) by an MCP server. MCP servers can be local or remote.
- The application accessing those services incorporates an MCP client.
- The MCP client is configured to connect with one (or more) MCP servers.
- The MCP client discovers available tools that the MCP server advertises.

These tools represent discrete functionality that the MCP server (and backing service) provides. The tools are described in a structured manner so that the LLM can understand, in detail, the advertised functionality. In short, the LLM knows what each function can do and how to call it.

The YugabyteDB MCP Server is a lightweight Python-based server that allows LLMs like Anthropic's Claude to interact directly with your YugabyteDB database.

The YugabyteDB MCP server:

- Enables LLM-powered data exploration on YugabyteDB.
- Uses safe, read-only queries to avoid modifying production data.
- Supports tools like Claude Desktop, Cursor, and Windsurf out-of-the-box.
- Helps interact with live data using natural language.

Using YugabyteDB MCP, you can standardize tool integration and perform intuitive data exploration, without needing to write a single line of SQL.

## Prerequisites

- YugabyteDB v2.25.1 or later
- Python 3.10+
- [uv](https://docs.astral.sh/uv/) for dependency management
- [Claude Desktop](https://claude.ai/download)

## Set up YugabyteDB MCP Server

Clone the repo and install dependencies:

```sh
git clone https://github.com/yugabyte/yugabytedb-mcp-server.git
cd yugabytedb-mcp-server
uv sync
```

## Set up YugabyteDB

1. [Download and install](https://download.yugabyte.com) YugabyteDB v2.25.1 or later.

1. Start a single-node cluster using [yugabyted](../../../../reference/configuration/yugabyted/).

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.1
    ```

1. [Install the Northwind sample database](../../../../sample-data/northwind/#install-the-northwind-sample-database).

## Connect Claude to the server

1. In Claude Desktop, navigate to **Settings > Developer > Edit Config**.

1. Add a new mcpServer entry in the `claude_config.json`:

    ```json
    {
        "mcpServers": {
            "yugabytedb-mcp": {
                "command": "uv",
                "args": [
                    "--directory",
                    "/path/to/cloned/yugabytedb-mcp-server/",
                    "run",
                    "src/server.py"
                ],
                "env": {
                    "YUGABYTEDB_URL": "dbname=northwind host=localhost port=5433 user=yugabyte password=yugabyte"
                }
            }
        }
    }
    ```

    Replace the path, and database user and password as appropriate.

1. Restart Claude to apply changes.

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
This appears to be the classic Northwind database, a sample database commonly used for demos and tutorials. Here's a summary:

Database Overview
This is a typical e-commerce/trading company database with 14 tables and approximately 3,400 total rows of data.

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

This database supports typical business operations like order management, inventory tracking, employee territories, and supplier relationships. Would you like me to run any specific queries or analysis on this data?
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

- Executes safe, read-only SQL queries.
- Aggregates and structures the results.
- Creates an interactive dashboard for the results.

All with no SQL required.

Example output:

```output
I've created an interactive sales dashboard with all three visualizations you requested:

1. Monthly Sales Trend - A line chart showing revenue progression from July 1996 to May 1998. You can see a clear upward trend with revenue growing from ~$27k to peaks of ~$123k per month.
2. Top 10 Customers by Revenue - A horizontal bar chart showing QUICK-Stop, Ernst Handel, and Save-a-lot Markets as the top three customers, each generating over $100k in revenue.
3. Sales by Country - A scrollable horizontal bar chart displaying all 21 countries, with USA and Germany leading at $245k and $230k respectively.

The dashboard features:

- Interactive tooltips showing exact values when you hover over data points
- Clean, modern design with a professional color scheme
- Responsive layout that adapts to different screen sizes
- Currency formatting for easy readability

All data is pulled directly from your Northwind database and shows real sales patterns over the ~2-year period covered by the dataset.
```

## Read more

- [YugabyteDB MCP Server](https://github.com/yugabyte/yugabytedb-mcp-server)
- [Architecting GenAI and RAG Apps with YugabyteDB](https://www.yugabyte.com/ai/)
