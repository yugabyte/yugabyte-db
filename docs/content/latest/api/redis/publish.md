---
title: PUBLISH
linkTitle: PUBLISH
description: PUBLISH
menu:
  latest:
    parent: api-redis
    weight: 2551
aliases:
  - api/redis/publish
  - api/yedis/publish
---

## Synopsis
<b>`PUBLISH channel message`</b><br>
This command publishes the given message to the specified channel. All subscribers that are subscribed to the specified channel
across all the YugaByte YEDIS API server(s) in the cluster will receive the message.


## Return Value
Returns, as an integer value, the number of subscribers that the message was forwarded to.

## Examples
```{.sh .copy .separator-dollar}
$ PUBLISH channel message
```
```sh
2
```

## See Also
[`pubsub`](../pubsub/), 
[`subscribe`](../subscribe/), 
[`unsubscribe`](../unsubscribe/), 
[`psubscribe`](../psubscribe/), 
[`punsubscribe`](../punsubscribe/)
