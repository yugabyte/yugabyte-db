To create a local cluster with the preceding configuration, use the following yugabyted commands:

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP1>/yugabyte-data \
  --listen=<IP1>                                \
  --master_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1a" \
  --tserver_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1a"
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP2>/yugabyte-data \
  --listen=<IP2>                                \
  --join=<IP1>                                  \
  --master_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1b" \
  --tserver_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1b"
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP3>/yugabyte-data \
  --listen=<IP3>                                \
  --join=<IP1>                                  \
  --master_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1c" \
  --tserver_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1c"
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP4>/yugabyte-data \
  --listen=<IP4>                                \
  --join=<IP1>                                  \
  --tserver_flags "placement_cloud=aws,placement_region=ap-south-1,placement_zone=ap-south-1a"
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP5>/yugabyte-data \
  --listen=<IP5>                                \
  --join=<IP1>                                  \
  --tserver_flags "placement_cloud=aws,placement_region=eu-west-2,placement_zone=eu-west-2c"
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP6>/yugabyte-data \
  --listen=<IP6>                                \
  --join=<IP1>                                  \
  --tserver_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1a"
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP7>/yugabyte-data \
  --listen=<IP7>                                \
  --join=<IP1>                                  \
  --tserver_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1a"
```

After cluster creation, verify that the nodes have been created with the given configuration by navigating to the Tablet Servers page in the YB-Master UI.

![YB Master UI - Tablet Servers Page](/images/explore/tablespaces/Geo_distributed_cluster_nodes_Master_UI.png)
