---
title: SUBSCRIBE
linkTitle: SUBSCRIBE
description: SUBSCRIBE
menu:
  preview:
    parent: api-yedis
    weight: 2552
aliases:
  - /preview/api/redis/subscribe
  - /preview/api/yedis/subscribe
type: docs
---

`SUBSCRIBE`

## Synopsis

**`SUBSCRIBE channel [channel ...]`**

This command subscribes the client to the specified channel(s). The client will receive a message whenever a
publisher sends a message to any of the channels that it has subscribed to.

## See also

[`pubsub`](../pubsub/),
[`publish`](../publish/),
[`subscribe`](../subscribe/),
[`unsubscribe`](../unsubscribe/),
[`psubscribe`](../psubscribe/),
[`punsubscribe`](../punsubscribe/)
