---
title: YSQL Connection Manager
headerTitle: YSQL Connection Manager
linkTitle: YSQL Connection Manager
description: Built-in server-side connection pooler for YSQL
headcontent: Built-in server-side connection pooler for YSQL
menu:
  v2025.1:
    identifier: connection-manager
    parent: additional-features
    weight: 10
type: indexpage
cascade:
  tags:
    feature: early-access
---

YugabyteDB includes a built-in connection pooler, YSQL Connection Manager. Because the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections without additional third-party tools. When combined with [smart drivers](/stable/develop/drivers-orms/smart-drivers/), YSQL Connection Manager simplifies application architecture and enhances developer productivity.

{{<index/block>}}

  {{<index/item
    title="Set up Connection Manager"
    body="Enable and configure built-in connection pooling."
    href="ycm-setup/"
    icon="fa-thin fa-sliders">}}

  {{<index/item
    title="Best practices"
    body="Optimize application performance for YSQL Connection Manager."
    href="ycm-best-practices/"
    icon="fa-thin fa-thumbs-up">}}

  {{<index/item
    title="Observability"
    body="Monitor your connections."
    href="ycm-monitor/"
    icon="fa-thin fa-chart-line">}}

  {{<index/item
    title="Migrate"
    body="Migrate from your current pooling solution."
    href="ycm-migrate/"
    icon="fa-thin fa-truck-moving">}}

  {{<index/item
    title="Troubleshoot"
    body="Troubleshoot connection problems."
    href="ycm-troubleshoot/"
    icon="fa-thin fa-screwdriver-wrench">}}

{{</index/block>}}
