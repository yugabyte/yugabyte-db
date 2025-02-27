To create a local cluster with the preceding configuration, use the following yugabyted commands:

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP1>/yugabyte-data \
  --advertise_address=<IP1>                     \
  --cloud_location=aws.us-east-1.us-east-1a     \
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP2>/yugabyte-data \
  --advertise_address=<IP2>                     \
  --join=<IP1>                                  \
  --cloud_location=aws.us-east-1.us-east-1b     \
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP3>/yugabyte-data \
  --advertise_address=<IP3>                     \
  --join=<IP1>                                  \
  --cloud_location=aws.us-east-1.us-east-1c     \
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP4>/yugabyte-data \
  --advertise_address=<IP4>                     \
  --join=<IP1>                                  \
  --cloud_location=aws.ap-south-1.ap-south-1a     \
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP5>/yugabyte-data \
  --advertise_address=<IP5>                     \
  --join=<IP1>                                  \
  --cloud_location=aws.eu-west-2.eu-west-2c     \
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP6>/yugabyte-data \
  --advertise_address=<IP6>                     \
  --join=<IP1>                                  \
  --cloud_location=aws.us-east-1.us-east-1a     \
```

```sh
./bin/yugabyted start                           \
  --base_dir=/home/yugabyte/<IP7>/yugabyte-data \
  --advertise_address=<IP7>                     \
  --join=<IP1>                                  \
  --cloud_location=aws.us-east-1.us-east-1a     \
```

After cluster creation, verify that the nodes have been created with the given configuration by navigating to the Tablet Servers page in the YB-Master UI.

![YB Master UI - Tablet Servers Page](/images/explore/tablespaces/Geo_distributed_cluster_nodes_Master_UI.png)
