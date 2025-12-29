---
title: YSQL Connection Manager
headerTitle: YSQL Connection Manager
linkTitle: YSQL Connection Manager
description: Built-in server-side connection pooler for YSQL
headcontent: Built-in server-side connection pooler for YSQL
menu:
  v2.20:
    identifier: connection-manager
    parent: additional-features
    weight: 10
type: indexpage
cascade:
  tags:
    feature: tech-preview
---

YugabyteDB includes a built-in connection pooler, YSQL Connection Manager. Because the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections without additional third-party tools. When combined with [smart drivers](/stable/develop/drivers-orms/smart-drivers/), YSQL Connection Manager simplifies application architecture and enhances developer productivity.

{{<index/block>}}

  {{<index/item
    title="Set up Connection Manager"
    body="Enable and configure built-in connection pooling."
    href="ycm-setup/"
    icon="fa-thin fa-sliders">}}

{{</index/block>}}
