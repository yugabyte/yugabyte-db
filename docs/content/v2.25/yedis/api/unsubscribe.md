---
title: UNSUBSCRIBE
linkTitle: UNSUBSCRIBE
description: UNSUBSCRIBE
menu:
  v2.25:
    parent: api-yedis
    weight: 2553
aliases:
  - /stable/api/redis/unsubscribe
  - /stable/api/yedis/unsubscribe
type: docs
---

## Synopsis

**`UNSUBSCRIBE [channel [channel ...]]`**

This command unsubscribes the client from the specified channel(s).

- If no channel is specified, the client is unsubscribed from all channels that it has subscribed to.

## See also

[`pubsub`](../pubsub/),
[`publish`](../publish/),
[`subscribe`](../subscribe/),
[`unsubscribe`](../unsubscribe/),
[`psubscribe`](../psubscribe/),
[`punsubscribe`](../punsubscribe/)
