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

The YugabyteDB MCP Server is a lightweight Python-based server that allows LLMs like Anthropic's Claude to interact directly with your YugabyteDB database.

## What is MCP?

MCP is a new standard that enables large language models (LLMs) to interact with existing enterprise services in a consistent and standardized way.

Services may be data providers (such as YugabyteDB or file servers), actuators (such as email servers), or both. A deep-dive is beyond the scope of this post, but this high-level overview helps illustrate why MCP is such a valuable development.

One challenge current AI applications face is that they are only as effective as the data they were trained on, and can only produce results by generating text, audio, or video.

However, what happens if an LLM (as part of an AI application) needs to access more current data, or company-specific data? And what happens if an AI application needs to issue an email, schedule an event in a calendar, or otherwise take action? In short, how can an AI application access existing services?

In the past, every LLM and framework (such as LangChain) has had its own way of doing this. This lack of a standard, consistent interface to services created complexity for AI application development and slowed progress.

MCP directly addresses this problem by standardizing how AI applications use existing services.

Here's how it works:

- Existing services are encapsulated (or fronted by) MCP Servers
- Your app (the Host) incorporates an MCP Client.
- Your app's MCP Client is configured to connect with one (or more) MCP Servers
- The client discovers available tools that the MCP Server advertises. Tools are analogous to API endpoints, representing discrete functionality that the MCP Server (and backing service) provides.

The tools are also described in a structured manner so that the LLM can understand in detail advertised functionality; that is, so that the LLM knows what each function can do and how to call it.

MCP Servers can be local (communicating via standard input/output) or remote (communicating via SSE over the network).
Each tool is like a function with arguments and (optionally) a return value. MCP makes tool-augmented LLMs scalable, consistent, and composable.

## Why Choose the YugabyteDB MCP Server?

The YugabyteDB MCP Server bridges the gap between your AI application (and its LLM) and your YugabyteDB data, enabling seamless, natural language interaction with live, structured datasets.

The YugabyteDB MCP server:

- Enables LLM-powered data exploration on YugabyteDB.
- Uses safe, read-only queries to avoid modifying production data.
- Supports tools like Claude Desktop, Cursor, and Windsurf out-of-the-box.
- Helps interact with live data using natural language.

Using MCP, you can standardize tool integration and perform intuitive data exploration, without needing to write a single line of SQL.

The following tutorial uses a YugabyteDB cluster running the [Northwind dataset](../../../../sample-data/northwind/). You will connect Claude to this database using the MCP protocol, then explore it using natural language prompts.

## Prerequisites

- YugabyteDB v2.25.1 or later
- Python 3.10+
- uv for dependency management
- [Claude Desktop](https://claude.ai/download)

## Set up the YugabyteDB MCP Server

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
                "YUGABYTEDB_URL": "dbname=northwind host=localhost port=5433 user=yugabyte password=yugabyte load_balance=true"
                }
            }
        }
    }
    ```

1. Restart Claude to apply changes.

You can view logs in the following location:

- macOS: ~/Library/Logs/Claude
- Linux: 

## Prompt 1: Summarize the database

Prompt:

```text
Summarize the database you are connected to.
```

Claude does the following:

- Calls `summarize_database`
- Lists all tables with schema and row counts
- Describes what it sees in plain English

Example output:

Summarize database

## Prompt 2: Build a dashboard

Prompt:

```text
Build a dashboard with 3 visualizations:

- Monthly sales trend
- Top 10 customers by revenue
- Sales grouped by customer country
```

Claude does the following:

- Executes safe, read-only SQL queries
- Aggregates and structures the results
- Creates an interactive dashboard for the results

All with no SQL required.

## Read more

- [Architecting GenAI and RAG Apps with YugabyteDB](https://www.yugabyte.com/ai/)
