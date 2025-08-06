---
title: YSQL Connection Manager
headerTitle: YSQL Connection Manager
linkTitle: YSQL Connection Manager
description: Built-in server-side connection pooler for YSQL
headcontent: Built-in server-side connection pooler for YSQL
tags:
  feature: early-access
menu:
  preview:
    identifier: connection-manager
    parent: launch-and-manage
    weight: 40
type: indexpage
---

YugabyteDB includes a built-in connection pooler, YSQL Connection Manager. Because the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections without additional third-party tools. When combined with [smart drivers](../drivers-orms/smart-drivers/), YSQL Connection Manager simplifies application architecture and enhances developer productivity.

{{<index/block>}}

  {{<index/item
    title="Set up connection manager"
    body="Learn how to enable YSQL Connection Manager and configure connection pooling."
    href="ycm-setup/"
    icon="fa-thin fa-dharmachakra">}}

  {{<index/item
    title="Best practices"
    body="Explore recommended configurations and usage patterns for optimizing your application's performance."
    href="ycm-best-practices/"
    icon="fa-thin fa-dharmachakra">}}

  {{<index/item
    title="Observability"
    body="Monitor your connections."
    href="ycm-monitor/"
    icon="fa-thin fa-globe-wifi">}}

  {{<index/item
    title="Migrate"
    body="Migrate from your current pooling solution."
    href="ycm-migrate/"
    icon="fa-thin fa-dharmachakra">}}

  {{<index/item
    title="Troubleshoot"
    body="Troubleshoot connection problems."
    href="ycm-troubleshoot/"
    icon="fa-thin fa-dharmachakra">}}

{{</index/block>}}
