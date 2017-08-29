---
title: YugaByte Query Layer (YQL)
summary: YQL overall description.
---
<style>
table {
  float: left;
}
</style>

YQL is a language processor with different adapters to support different APIs including but not limited to REDIS and CQL. Applications such as Redis-Cli or cqlsh can connect, send requests, and receive responses from YQL. YQL recieves requests from various clients of various APIs, translates them into a set of instructions for Yugabyte Database System to execute, and sends the result back to clients.

The following table shows development status for API adapters.

| API | Status |
|-----|--------|
| Redis | Most commands for strings, hashes, and sets are supported |
| Apache CQL | All system-defined datatypes and most features are supported |
