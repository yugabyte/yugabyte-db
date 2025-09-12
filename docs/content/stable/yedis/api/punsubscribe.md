---
title: PUNSUBSCRIBE
linkTitle: PUNSUBSCRIBE
description: PUNSUBSCRIBE
menu:
  preview:
    parent: api-yedis
    weight: 2555
aliases:
  - /preview/api/redis/punsubscribe
  - /preview/api/yedis/punsubscribe
type: docs
---

## Synopsis

**`PUNSUBSCRIBE [pattern [pattern ...]]`**

This command unsubscribes the client from the specified pattern(s). If no pattern is specified, the client is unsubscribed from all patterns that it has subscribed to.

## See also

[`keys`](../keys/),
[`pubsub`](../pubsub/),
[`publish`](../publish/),
[`subscribe`](../subscribe/),
[`unsubscribe`](../unsubscribe/),
[`psubscribe`](../psubscribe/),
[`punsubscribe`](../punsubscribe/)
