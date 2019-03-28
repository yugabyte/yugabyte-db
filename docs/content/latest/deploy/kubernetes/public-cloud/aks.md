---
---

```sh
$ helm install yugabyte -f expose-all.yaml --namespace yb-demo --name yb-demo \
  --set annotations.tserver.loadbalancer."service\.beta\.kubernetes\.io/aws-load-balancer-internal"=0.0.0.0/0 --wait
```
