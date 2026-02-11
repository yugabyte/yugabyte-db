---
title: Yugabyte Docs MCP Server
headerTitle: Yugabyte Docs MCP Server
linkTitle: Docs MCP Server
description: Access YugabyteDB documentation directly from your IDE or AI tool.
menu:
   stable_develop:
    identifier: tutorials-docs-mcp-server
    parent: tutorials-ai-agentic
    weight: 30
type: docs
---


The Yugabyte Docs MCP Server enables you to access Yugabyte documentation directly from your IDE or AI tool (like ChatGPT, Claude, Cursor, or VS Code).

By connecting this server to the AI tools from your IDE (or CLI), you give your AI assistant direct access to Yugabyte official documentation, API references, blogs and support articles.

This enables the tool to answer questions about Yugabyte without leaving your IDE.

## Set up

The MCP server is hosted at `https://yugabyte.mcp.kapa.ai`.

From the docs Search bar, click **Ask AI > Use MCP**. In the **Connect to AI tools** dropdown, you can add the Yugabyte Docs MCP server by choosing the option that matches your tool.

{{< tabpane text=true >}}

  {{% tab header="Cursor" lang="cursor" %}}

The **Add to Cursor** option opens Cursor directly with the MCP server configuration **Tools** window. You need to do the following additional steps to enable the server:

1. Click **Install** in the **Install MCP Server?** dialog box.
1. Click **Connect** beside the yugabyte-docs server and you should see a green dot with the tool enabled.
1. If you get a **Needs authentication** sign, click the option and restart Cursor to apply the changes.

  {{% /tab %}}

  {{% tab header="VS Code" lang="vscode" %}}

**Prerequisites**: VS Code 1.102+ with GitHub Copilot enabled.

The **Add to VS Code** option opens VS Code directly with the MCP server configuration window. You need to do the following additional steps to enable the server:

1. Click **Install** to install the yugabyte-docs MCP server.
1. Go to **Extensions** to check yugabyte-docs listed as an installed MCP server.
1. Open the Chat view (Ctrl+Cmd+I / Ctrl+Alt+I), and select **Agent** mode from the dropdown.
1. Click the Tools button to verify that yugabyte-docs is selected in the available MCP tools.

  {{% /tab %}}

  {{% tab header="Claude Code" lang="claude" %}}

1. Use the CLI command to register the Docs MCP server with the Claude Code agent.

1. Confirm the server is listed:

    ```bash
    claude mcp list
    ```

  {{% /tab %}}

  {{% tab header="MCP URL" lang="mcp-url" %}}

For Claude Desktop, ChatGPT, or other MCP-compatible clients, you can use the hosted endpoint or a local config.

**Option A — Kapa-hosted endpoint**

Use this URL in clients that accept an MCP server URL:

`https://yugabyte.mcp.kapa.ai`

**Option B — Local config (for example, Claude Desktop)**

Add the server to your client config (for example, `claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "yugabytedb-docs": {
      "command": "npx",
      "args": ["-y", "@yugabyte/mcp-server-docs"]
    }
  }
}
```

Restart Claude Desktop for changes to take effect.

  {{% /tab %}}

{{< /tabpane >}}

## Examples

After the server is connected, try prompts such as:
* Explain the Raft consensus implementation in YugabyteDB.
* Give me a YSQL example of a stored procedure that handles a bank transfer with error checking.
* What is the recommended way to perform a rolling upgrade on a 6-node cluster?
* Summarize the main differences between YSQL and YCQL index types.
