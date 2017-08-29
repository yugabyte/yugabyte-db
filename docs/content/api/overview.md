---
title: YugaByte API
summary: Overall description.
---
<style>
table {
  float: left;
}
</style>
This section discuss how clients can access data by interacting with YugaByte Server. YugaByte software implements different adapters to support different APIs including but not limited to REDIS and CQL. Applications such as Redis-Cli or cqlsh can connect with YugaByte server to send requests and receive responses. After receiving requests of various APIs, YugaByte language adapters translate them into a set of instructions for Yugabyte Database System to execute, and sends the execution result back to clients.

The following table shows development status for the language adapters.

| API | Status |
|-----|--------|
| Redis | Most commands for strings, hashes, and sets are supported |
| Apache CQL | All system-defined datatypes and most features are supported |
