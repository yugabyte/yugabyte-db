## yba universe create

Create YugabyteDB Anywhere universe

### Synopsis

Create an universe in YugabyteDB Anywhere

```
yba universe create [flags]
```

### Options

```
  -n, --name string                                      [Required] The name of the universe to be created.
      --provider-code string                             [Required] Provider code. Allowed values: aws, gcp, azu, onprem, kubernetes.
      --provider-name string                             [Optional] Provider name to be used in universe. Run "yba provider list --code <provider-code>" to check the default provider for the given provider-code.
      --dedicated-nodes                                  [Optional] Place Masters on dedicated nodes, (default false) for aws, azu, gcp, onprem. Defaults to true for kubernetes.
      --add-read-replica                                 [Optional] Add a read replica cluster to the universe. (default false)
      --replication-factor ints                          [Optional] Replication factor of the cluster. Provide replication-factor for each cluster as a separate flag. "--replication-factor 3 --replication-factor 5" OR "--replication-factor 3,5" refers to RF of Primary cluster = 3 and RF of Read Replica = 5. First flag always corresponds to the primary cluster. (default [3,3])
      --num-nodes ints                                   [Optional] Number of nodes in the cluster. Provide no of nodes for each cluster as a separate flag. "--num-nodes 3 --num-nodes 5" OR "--num-nodes 3,5" refers to 3 nodes in the Primary cluster and 5 nodes in the Read Replica cluster. First flag always corresponds to the primry cluster. (default [3,3])
      --regions stringArray                              [Optional] Regions for the nodes of the cluster to be placed in. Provide comma-separated strings for each cluster as a separate flag, in the following format: "--regions 'region-1-for-primary-cluster,region-2-for-primary-cluster' --regions 'region-1-for-read-replica,region-2-for-read-replica'". Defaults to fetching the region from the provider. Throws an error if multiple regions are present.
      --preferred-region stringArray                     [Optional] Preferred region to place the node of the cluster in. Provide preferred regions for each cluster as a separate flag. Defaults to null.
      --master-gflags string                             [Optional] Master GFlags. Provide comma-separated key-value pairs for the primary cluster in the following format: "--master-gflags master-gflag-key-1=master-gflag-value-1,master-gflag-key-2=master-gflag-key2".
      --tserver-gflags stringArray                       [Optional] TServer GFlags. Provide comma-separated key-value pairs for each cluster as a separate flag in the following format: "--tserver-gflags tserver-gflag-key-1-for-primary-cluster=tserver-gflag-value-1,tserver-gflag-key-2-for-primary-cluster=tserver-gflag-key2 --tserver-gflags tserver-gflag-key-1-for-read-replica=tserver-gflag-value-1,tserver-gflag-key-2-for-read-replica=tserver-gflag-key2". If no-of-clusters = 2 and no tserver gflags are provided for the read replica, the primary cluster gflags are by default applied to the read replica cluster.
      --instance-type stringArray                        [Optional] Instance Type for the universe nodes. Provide the instance types for each cluster as a separate flag. Defaults to "c5.large" for aws, "Standard_DS2_v2" for azure and "n1-standard-1" for gcp. Fetches the first available instance type for onprem providers.
      --num-volumes ints                                 [Optional] Number of volumes to be mounted on this instance at the default path. Provide the number of volumes for each cluster as a separate flag or as comma separated values. (default [1,1])
      --volume-size ints                                 [Optional] The size of each volume in each instance. Provide the number of volumes for each cluster as a separate flag or as comma separated values. (default [100,100])
      --mount-points stringArray                         [Optional] Disk mount points. Provide comma-separated strings for each cluster as a separate flag, in the following format: "--mount-points 'mount-point-1-for-primary-cluster,mount-point-2-for-primary-cluster' --mount-points 'mount-point-1-for-read-replica,mount-point-2-for-read-replica'". Defaults to null for aws, azure, gcp. Fetches the first available instance mount points for onprem providers.
      --storage-type stringArray                         [Optional] Storage type (EBS for AWS) used for this instance. Provide the storage type  of volumes for each cluster as a separate flag. Defaults to "GP3" for aws, "Premium_LRS" for azure and "Persistent" for gcp.
      --storage-class stringArray                        [Optional] Name of the storage class, supported for Kubernetes. Provide the storage type of volumes for each cluster as a separate flag. Defaults to "standard".
      --disk-iops ints                                   [Optional] Desired IOPS for the volumes mounted on this instance, supported only for AWS. Provide the number of volumes for each cluster as a separate flag or as comma separated values. (default [3000,3000])
      --throughput ints                                  [Optional] Desired throughput for the volumes mounted on this instance in MB/s, supported only for AWS. Provide throughput for each cluster as a separate flag or as comma separated values. (default [125,125])
      --k8s-tserver-mem-size float64Slice                [Optional] Memory size of the kubernetes tserver node in GB. Provide k8s-tserver-mem-size for each cluster as a separate flag or as comma separated values. (default [4.000000,4.000000])
      --k8s-tserver-cpu-core-count float64Slice          [Optional] CPU core count of the kubernetes tserver node. Provide k8s-tserver-cpu-core-count for each cluster as a separate flag or as comma separated values. (default [2.000000,2.000000])
      --dedicated-master-instance-type string            [Optional] Instance Type for the dedicated master nodes in the primary cluster. Defaults to "c5.large" for aws, "Standard_DS2_v2" for azure and "n1-standard-1" for gcp. Fetches the first available instance type for onprem providers.
      --dedicated-master-num-volumes int                 [Optional] Number of volumes to be mounted on master instance at the default path. (default 1)
      --dedicated-master-volume-size int                 [Optional] The size of each volume in each master instance. (default 100)
      --dedicated-master-mount-points string             [Optional] Disk mount points for master nodes. Provide comma-separated strings in the following format: "--mount-points 'mount-point-1-for-master,mount-point-2-for-master'" Defaults to null for aws, azure, gcp. Fetches the first available instance mount points for onprem providers.
      --dedicated-master-storage-type string             [Optional] Storage type (EBS for AWS) used for master instance. Defaults to "GP3" for aws, "Premium_LRS" for azure and "Persistent" for gcp. Fetches the first available storage type for onprem providers.
      --dedicated-master-storage-class string            [Optional] Name of the storage class for the master instance. Defaults to "standard".
      --dedicated-master-disk-iops int                   [Optional] Desired IOPS for the volumes mounted on this instance, supported only for AWS. (default 3000)
      --dedicated-master-throughput int                  [Optional] Desired throughput for the volumes mounted on this instance in MB/s, supported only for AWS. (default 125)
      --k8s-master-mem-size float64Slice                 [Optional] Memory size of the kubernetes master node in GB. Provide k8s-tserver-mem-size for each cluster as a separate flag or as comma separated values. (default [4.000000,4.000000])
      --k8s-master-cpu-core-count float64Slice           [Optional] CPU core count of the kubernetes master node. Provide k8s-tserver-cpu-core-count for each cluster as a separate flag or as comma separated values. (default [2.000000,2.000000])
      --assign-public-ip                                 [Optional] Assign Public IPs to the DB servers for connections over the internet. (default true)
      --enable-ysql                                      [Optional] Enable YSQL endpoint. (default true)
      --ysql-password string                             [Optional] YSQL authentication password. Use single quotes ('') to provide values with special characters.
      --enable-ycql                                      [Optional] Enable YCQL endpoint. (default true)
      --ycql-password string                             [Optional] YCQL authentication password. Use single quotes ('') to provide values with special characters.
      --enable-yedis                                     [Optional] Enable YEDIS endpoint. (default false)
      --enable-node-to-node-encrypt                      [Optional] Enable Node-to-Node encryption to use TLS enabled connections for communication between different Universe nodes. (default true)
      --enable-client-to-node-encrypt                    [Optional] Enable Client-to-Node encryption to use TLS enabled connection for communication between a client (ex: Database application, ysqlsh, ycqlsh) and the Universe YSQL -or- YCQL endpoint. (default true)
      --root-ca string                                   [Optional] Root Certificate name for Encryption in Transit, defaults to creating new certificate for the universe if encryption in transit in enabled.
      --enable-volume-encryption                         [Optional] Enable encryption for data stored on the tablet servers. (default false)
      --kms-config string                                [Optional] Key management service config name. Required when enable-volume-encryption is set to true.
      --enable-ipv6                                      [Optional] Enable IPV6 networking for connections between the DB Servers, supported only for Kubernetes universes (default false) 
      --yb-db-version string                             [Optional] YugabyteDB Software Version, defaults to the latest available version. Run "yba yb-db-version list" to find the latest version.
      --use-systemd                                      [Optional] Use SystemD. (default true)
      --access-key-code string                           [Optional] Access Key code (UUID) corresponding to the provider, defaults to the provider's access key.
      --aws-arn-string string                            [Optional] Instance Profile ARN for AWS universes.
      --user-tags stringToString                         [Optional] User Tags for the DB instances. Provide as key=value pairs per flag. Example "--user-tags name=test --user-tags owner=development" OR "--user-tags name=test,owner=development". (default [])
      --kubernetes-universe-overrides-file-path string   [Optional] Helm Overrides file path for the universe, supported for Kubernetes. For examples on universe overrides file contents, please refer to: "https://docs.yugabyte.com/stable/yugabyte-platform/create-deployments/create-universe-multi-zone-kubernetes/#configure-helm-overrides"
      --kubernetes-az-overrides-file-path stringArray    [Optional] Helm Overrides file paths for the availabilty zone, supported for Kubernetes. Provide file paths for overrides of each Availabilty zone as a separate flag. For examples on availabilty zone overrides file contents, please refer to: "https://docs.yugabyte.com/stable/yugabyte-platform/create-deployments/create-universe-multi-zone-kubernetes/#configure-helm-overrides"
      --master-http-port int                             [Optional] Master HTTP Port. (default 7000)
      --master-rpc-port int                              [Optional] Master RPC Port. (default 7100)
      --node-exporter-port int                           [Optional] Node Exporter Port. (default 9300)
      --redis-server-http-port int                       [Optional] Redis Server HTTP Port. (default 11000)
      --redis-server-rpc-port int                        [Optional] Redis Server RPC Port. (default 6379)
      --tserver-http-port int                            [Optional] TServer HTTP Port. (default 9000)
      --tserver-rpc-port int                             [Optional] TServer RPC Port. (default 9100)
      --yql-server-http-port int                         [Optional] YQL Server HTTP Port. (default 12000)
      --yql-server-rpc-port int                          [Optional] YQL Server RPC Port. (default 9042)
      --ysql-server-http-port int                        [Optional] YSQL Server HTTP Port. (default 13000)
      --ysql-server-rpc-port int                         [Optional] YSQL Server RPC Port. (default 5433)
  -h, --help                                             help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe](yba_universe.md)	 - Manage YugabyteDB Anywhere universes

