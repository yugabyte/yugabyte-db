## Create a 3 node cluster with replication factor 3 

We will use the `yb-docker-ctl` utility downloaded in the previous step to create and administer a containerized local cluster. Detailed output for the *create* command is available in [yb-docker-ctl Reference](/admin/yb-docker-ctl/#create-cluster).

```sh
$ ./yb-docker-ctl create
```

Clients can now connect to YugaByte's CQL service at `localhost:9042` and to YugaByte's Redis service at  `localhost:6379`.

## Check the status of the cluster

Run the command below to see that we now have 3 `yb-master` (yb-master-n1,yb-master-n2,yb-master-n3) and 3 `yb-tserver` (yb-tserver-n1,yb-tserver-n2,yb-tserver-n3) containers running on this localhost. Roles played by these containers in a YugaByte cluster (aka Universe) is explained in detail [here](/architecture/concepts/#universe-components).

```sh
$ ./yb-docker-ctl status
PID        Type       Node       URL                       Status          Started At          
26132      tserver    n3         http://172.18.0.7:9000    Running         2017-10-20T17:54:54.99459154Z
25965      tserver    n2         http://172.18.0.6:9000    Running         2017-10-20T17:54:54.412377451Z
25846      tserver    n1         http://172.18.0.5:9000    Running         2017-10-20T17:54:53.806993683Z
25660      master     n3         http://172.18.0.4:7000    Running         2017-10-20T17:54:53.197652566Z
25549      master     n2         http://172.18.0.3:7000    Running         2017-10-20T17:54:52.640188158Z
25438      master     n1         http://172.18.0.2:7000    Running         2017-10-20T17:54:52.084772289Z
```

The admin UI for yb-master-n1 is available at http://localhost:9000 and the admin UI fo yb-tserver-n1 is available at http://localhost:7000. Other masters and tservers do not have their admin ports mapped to localhost to avoid port conflicts.
