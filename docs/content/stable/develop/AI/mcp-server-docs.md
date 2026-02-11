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

The Yugabyte Docs MCP Server implements the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) to give AI tools (like ChatGPT, Claude, Cursor, or VS Code) direct access to the Yugabyte knowledge base.

By connecting this server to the AI tools from your IDE (or CLI), you give your AI assistant direct access to official documentation, API references, blogs and support articles—enabling it to answer complex technical questions and generate accurate code without ever leaving your IDE.

## Set up

The MCP server is hosted at `https://yugabyte.mcp.kapa.ai`.

From the docs Search bar, click **Ask AI > Use MCP**. In the **Connect to AI tools** dropdown, you can choose from one of the four ways to add the Yugabyte Docs MCP server to your environment. Use the tab that matches your tool.

{{< tabpane lang="bash" >}}
  {{< tab header="Cursor" >}}

1. Open **Cursor Settings** (`Cmd + Shift + J` on macOS, `Ctrl + Shift + J` on Windows/Linux).
2. Go to **Features** → **MCP**.
3. Click **+ Add new MCP server**.
4. Enter:
   * **Name:** `yugabytedb-docs`
   * **Type:** `command`
   * **Command:** `npx -y @yugabyte/mcp-server-docs`
5. Click **Save**.

{{< /tab >}}

  {{< tab header="VS Code" >}}

1. Open the **Command Palette** (`Cmd + Shift + P` on macOS, `Ctrl + Shift + P` on Windows/Linux).
2. Run **MCP: Add Server**.
3. When prompted, paste:

   ```bash
   npx -y @yugabyte/mcp-server-docs
   ```

4. Restart your Copilot or AI chat session so it picks up the new server.

{{< /tab >}}

{{< tab header="Claude Code" >}}

Use the CLI command to register the Docs MCP server with the Claude Code agent.

1. Run:

   ```bash
   claude mcp add yugabytedb-docs -- npx -y @yugabyte/mcp-server-docs
   ```

2. Confirm the server is listed:

```bash
claude mcp list
```

{{< /tab >}}

{{< tab header="MCP URL" >}}

### Use the MCP URL or config in other clients

For Claude Desktop, ChatGPT, or other MCP-compatible clients, you can use the hosted endpoint or a local config.

**Option A — Kapa-hosted endpoint**

Use this URL in clients that accept an MCP server URL:

`https://mcp.kapa.ai/v0/yugabyte`

**Option B — Local config (e.g. Claude Desktop)**

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

{{< /tab >}}
{{< /tabpane >}}

## Example prompts

After the server is connected, try prompts such as:

* "What are the default isolation levels in YugabyteDB?"
* "Provide a Kubernetes manifest for a 3-node YugabyteDB cluster."
* "Explain the differences between YSQL and YCQL indexing."
