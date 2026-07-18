To create a local cluster with the preceding configuration, use the following yugabyted commands:

```sh
./bin/yugabyted start                               \
  --base_dir=/home/yugabyte/127.0.0.1/yugabyte-data \
  --advertise_address=127.0.0.1                     \
  --cloud_location=aws.us-east-1.us-east-1a

./bin/yugabyted start                               \
  --base_dir=/home/yugabyte/127.0.0.2/yugabyte-data \
  --advertise_address=127.0.0.2                     \
  --join=127.0.0.1                                  \
  --cloud_location=aws.us-east-1.us-east-1b

./bin/yugabyted start                               \
  --base_dir=/home/yugabyte/127.0.0.3/yugabyte-data \
  --advertise_address=127.0.0.3                     \
  --join=127.0.0.1                                  \
  --cloud_location=aws.us-east-1.us-east-1c

./bin/yugabyted start                               \
  --base_dir=/home/yugabyte/127.0.0.4/yugabyte-data \
  --advertise_address=127.0.0.4                     \
  --join=127.0.0.1                                  \
  --cloud_location=aws.ap-south-1.ap-south-1a

./bin/yugabyted start                               \
  --base_dir=/home/yugabyte/127.0.0.5/yugabyte-data \
  --advertise_address=127.0.0.5                     \
  --join=127.0.0.1                                  \
  --cloud_location=aws.eu-west-2.eu-west-2c

./bin/yugabyted start                               \
  --base_dir=/home/yugabyte/127.0.0.6/yugabyte-data \
  --advertise_address=127.0.0.6                     \
  --join=127.0.0.1                                  \
  --cloud_location=aws.us-east-1.us-east-1a

./bin/yugabyted start                               \
  --base_dir=/home/yugabyte/127.0.0.7/yugabyte-data \
  --advertise_address=127.0.0.7                     \
  --join=127.0.0.1                                  \
  --cloud_location=aws.us-east-1.us-east-1a
```

After cluster creation, verify that the nodes have been created with the given configuration by navigating to the Tablet Servers page in the YB-Master UI.

![YB Master UI - Tablet Servers Page](/images/explore/tablespaces/Geo_distributed_cluster_nodes_Master_UI.png)
