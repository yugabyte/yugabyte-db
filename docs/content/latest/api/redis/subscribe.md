---
title: SUBSCRIBE
linkTitle: SUBSCRIBE
description: SUBSCRIBE
menu:
  latest:
    parent: api-redis
    weight: 2552
aliases:
  - /latest/api/redis/subscribe
  - /latest/api/yedis/subscribe
---
`SUBSCRIBE` 

## Synopsis
<b>`SUBSCRIBE channel [channel ...]`</b><br>
This command subscribes the client to the specified channel(s). The client will receive a message whenever a
publisher sends a message to any of the channels that it has subscribed to.

## See Also
[`pubsub`](../pubsub/), 
[`publish`](../publish/), 
[`subscribe`](../subscribe/), 
[`unsubscribe`](../unsubscribe/), 
[`psubscribe`](../psubscribe/), 
[`punsubscribe`](../punsubscribe/)
