## yba universe create-read-replica

Create a read replica for existing YugabyteDB universe

### Synopsis

Create a read replica for existing YugabyteDB universe

```
yba universe create-read-replica [flags]
```

### Options

```
  -n, --name string                        [Required] Name of the universe to add read replica to.
      --replication-factor int             [Optional] Number of read replicas to be added. (default 3)
      --num-nodes int                      [Optional] Number of nodes in the read replica universe. (default 3)
      --regions string                     [Optional] Regions to add read replica to. Defaults to primary cluster regions.
      --preferred-region string            [Optional] Preferred region to place the node of the cluster in.
      --zones stringArray                  [Optional] Zones to add read replica nodes to. Defaults to primary cluster zones. Provide the following double colon (::) separated fields as key-value pairs: "--zones 'zone-name=<zone1>::region-name=<region1>::num-nodes=<number-of-nodes-to-be-placed-in-zone>" Each zone must have the region and number of nodes to be placed in that zone. Add the region via --regions flag if not present in the universe. Each zone needs to be added using a separate --zones flag.
      --tserver-gflags string              [Optional] TServer GFlags in map (JSON or YAML) format. Provide the gflags in the following formats: "--tserver-gflags {"tserver-gflag-key-1":"value-1","tserver-gflag-key-2":"value-2" }" or  "--tserver-gflags "tserver-gflag-key-1: value-1
                                           tserver-gflag-key-2: value-2
                                           tserver-gflag-key-3: value-3". If no tserver gflags are provided for the read replica, the primary cluster gflags are by default applied to the read replica cluster.
      --linux-version string               [Optional] Linux version to be used for the read replica cluster. Default linux version is fetched from the primary cluster.
      --instance-type string               [Optional] Instance type to be used for the read replica cluster. Default instance type is fetched from the primary cluster.
      --num-volumes int                    [Optional] Number of volumes to be mounted on this instance at the default path. Default number of volumes is fetched from the primary cluster. (default 1)
      --volume-size int                    [Optional] The size of each volume in each instance.Default volume size is fetched from the primary cluster. (default 100)
      --mount-points string                [Optional] Disk mount points. Default disk mount points are fetched from the primary cluster.
      --storage-type string                [Optional] Storage type (EBS for AWS) used for this instance. Default storage type is fetched from the primary cluster.
      --storage-class string               [Optional]  Name of the storage class, supported for Kubernetes. Default storage class is fetched from the primary cluster.
      --disk-iops int                      [Optional] Desired IOPS for the volumes mounted on this instance, supported only for AWS. Default disk IOPS is fetched from the primary cluster. (default 3000)
      --throughput int                     [Optional] Desired throughput for the volumes mounted on this instance in MB/s, supported only for AWS. Default throughput is fetched from the primary cluster. (default 125)
      --k8s-tserver-mem-size float         [Optional] Memory size of the kubernetes tserver node in GB. Default memory size is fetched from the primary cluster. (default 4)
      --k8s-tserver-cpu-core-count float   [Optional] CPU core count of the kubernetes tserver node. Default CPU core count is fetched from the primary cluster. (default 2)
      --exposing-service string            [Optional] Exposing service for the universe clusters. Allowed values: none, exposed, unexposed. Default exposing service is fetched from the primary cluster.
      --user-tags stringToString           [Optional] User Tags for the DB instances. Provide as key-value pairs per flag. Example "--user-tags name=test --user-tags owner=development" OR "--user-tags name=test,owner=development". (default [])
  -s, --skip-validations                   [Optional] Skip validations before running the CLI command.
  -h, --help                               help for create-read-replica
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe](yba_universe.md)	 - Manage YugabyteDB Anywhere universes

