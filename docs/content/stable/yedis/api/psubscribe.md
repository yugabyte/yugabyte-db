---
title: PSUBSCRIBE
linkTitle: PSUBSCRIBE
description: PSUBSCRIBE
menu:
  stable:
    parent: api-yedis
    weight: 2554
aliases:
  - /stable/api/redis/psubscribe
  - /stable/api/yedis/psubscribe
type: docs
---

## Synopsis

**`PSUBSCRIBE pattern [pattern ...]`**

This command subscribes the client to the specified pattern(s). The client will receive a message whenever a publisher sends a message to a channel that matches any of the patterns that it has subscribed to.

## See also

[`keys`](../keys/),
[`pubsub`](../pubsub/),
[`publish`](../publish/),
[`subscribe`](../subscribe/),
[`unsubscribe`](../unsubscribe/),
[`psubscribe`](../psubscribe/),
[`punsubscribe`](../punsubscribe/)
