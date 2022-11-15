---
title: PUBSUB
linkTitle: PUBSUB
description: PUBSUB
menu:
  preview:
    parent: api-yedis
    weight: 2550
aliases:
  - /preview/api/redis/pubsub
  - /preview/api/yedis/pubsub
type: docs
---

## Synopsis

PUBSUB command is useful to view the state of the Pub/Sub system in Yugabyte. These commands only provide information regarding the specific YEDIS server that is queried. 3 subcommands are supported:

### `PUBSUB channels [pattern]`

Lists the currently active channels (optionally matching the given pattern) on the YEDIS server that is queried -- i.e. channels with one or more subscribers on the node that the client is connected to.

### `PUBSUB NUBSUB [channel-1 .. channel-n]`

Returns the number of subscribers for the specified channels (not counting the clients that are subscribed to patterns) on the YEDIS server that is queried.

### `PUBSUB NUMPAT`

Returns the number of patterns that are subscribed to on the YEDIS server that is queried.

## See also

[`pubsub`](../pubsub/),
[`publish`](../publish/),
[`subscribe`](../subscribe/),
[`unsubscribe`](../unsubscribe/),
[`psubscribe`](../psubscribe/),
[`punsubscribe`](../punsubscribe/)
