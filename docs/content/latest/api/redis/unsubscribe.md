---
title: UNSUBSCRIBE
linkTitle: UNSUBSCRIBE
description: UNSUBSCRIBE
menu:
  latest:
    parent: api-redis
    weight: 2553
aliases:
  - /latest/api/redis/unsubscribe
  - /latest/api/yedis/unsubscribe
---
`UNSUBSCRIBE` 

## Synopsis
<b>`UNSUBSCRIBE [channel [channel ...]]`</b><br>
This command unsubscribes the client from the specified channel(s). 
 If no channel is specified, the client is unsubscribed from all channels that it has subscribed to.

## See Also
[`pubsub`](../pubsub/), 
[`publish`](../publish/), 
[`subscribe`](../subscribe/), 
[`unsubscribe`](../unsubscribe/), 
[`psubscribe`](../psubscribe/), 
[`punsubscribe`](../punsubscribe/)
