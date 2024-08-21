---
title: Miscellaneous issues
linkTitle: Miscellaneous issues
menu:
  preview:
    identifier: troubleshoot-miscellaneous
    parent: troubleshoot
    weight: 400
type: docs
rightnav:
    hideh3: true
---

## Leader preference

### Tablets not being placed according to leader preference or placement policy

Leader preference is enforced by the [load balancer](../../architecture/yb-master#load-balancing) component of the [YB-Master](../../architecture/yb-master) service. This load balancer has a 2 minute delay in triggering after the cluster is started. For the tables created within this 2 minute window, it will take a while for the tablets to be placed according to the placement policy.

## yugabyted

### Unable to start yugabyted on mac os

When starting a local cluster on mac, you might see an error like:

```bash{.nocopy}
ERROR: Master node present at 127.0.0.1:7000 is not reachable.
```

This could be because, on the mac os the port 7000 is used by the Airplay Reciever service. This service allows other devices to stream content to your mac. You can disable this feature temporarily by going to System settings -> Airdrop -> Airplay Receiver and toggle it `OFF` and then start your cluster and then enable the Airplay Receiver feature.

For scenarios where you do not want to turn off the Airplay Receiver feature, you can start the master webserver on a different port using the `master_webserver_port` gflag.

{{<tip>}}
If you do not need this feature, it is advisable to permanently disable it.
{{</tip>}}
