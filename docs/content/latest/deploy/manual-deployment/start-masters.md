---
title: 3. Start YB-Masters
linkTitle: 3. Start YB-Masters
description: 3. Start YB-Masters
aliases:
  - /deploy/manual-deployment/start-masters
menu:
  latest:
    identifier: deploy-manual-deployment-start-masters
    parent: deploy-manual-deployment
    weight: 613
---

{{< note title="Note" >}}
For any cluster, the number of nodes on which the YB-Masters need to be started on **must** equal the replication factor.
{{< /note >}}

As an example scenario, let us assume the following.

- We want to create a a 4 node cluster with replication factor `3`.
      - We would need to run the YB-Master process on three nodes (`node-a`, `node-b`, `node-c`)
      - Let us assume their IP addresses are `172.151.17.130`, `172.151.17.220` and `172.151.17.140`
- We have multiple data drives mounted on `/home/centos/disk1`, `/home/centos/disk2`

This section covers deployment for a single region/zone (or a single datacenter/rack). Execute the following steps on each of the instances.

- Run `yb-master` binary on each of the nodes as shown below. Note how multiple directories can be provided to the `--fs_data_dirs` flag. For the full list of flags, see the [yb-master Reference](/admin/yb-master/). 

```{.sh .copy .separator-dollar}
$ ./bin/yb-master \
  --master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
  --fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
  >& /home/centos/disk1/yb-master.out &
```


- Alternatively, you can also create a `master.conf` file with the following flags and then run the `yb-master` with the `--flagfile` option as shown below.

```{.sh .copy}
--master_addresses=172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100
--fs_data_dirs=/home/centos/disk1,/home/centos/disk2 
```

```{.sh .copy .separator-dollar}
$ ./bin/yb-master --flagfile master.conf >& /home/centos/disk1/yb-master.out &
```

- Make sure all the 3 yb-masters are now working as expected by inspecting the INFO log. The default logs directory is always inside the first directory specified in the `--fs_data_dirs` flag.

```{.sh .copy .separator-dollar}
$ cat /home/centos/disk1/yb-data/master/logs/yb-master.INFO
```

You can see that the 3 yb-masters were able to discover each other and were also able to elect a Raft leader among themselves (the remaining two act as Raft followers).

For the masters that become followers, you will see the following line in the log.
```sh
I0912 16:11:07.419591  8030 sys_catalog.cc:332] T 00000000000000000000000000000000 P bc42e1c52ffe4419896a816af48226bc [sys.catalog]: This master's current role is: FOLLOWER
```

For the master that becomes the leader, you will see the following line in the log.
```sh
I0912 16:11:06.899287 27220 raft_consensus.cc:738] T 00000000000000000000000000000000 P 21171528d28446c8ac0b1a3f489e8e4b [term 2 LEADER]: Becoming Leader. State: Replica: 21171528d28446c8ac0b1a3f489e8e4b, State: 1, Role: LEADER
```

{{< tip title="Tip" >}}Remember to add the command with which you launched `yb-master` to a cron to restart it if it goes down.{{< /tip >}}<br>


Now we are ready to start the yb-tservers.
