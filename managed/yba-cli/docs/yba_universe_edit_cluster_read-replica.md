## yba universe edit cluster read-replica

Edit the Read replica Cluster in a YugabyteDB Anywhere universe

### Synopsis

Edit the Read replica Cluster in a YugabyteDB Anywhere universe.

```
yba universe edit cluster read-replica [flags]
```

### Examples

```
yba universe edit cluster --name <universe-name> read-replica --num-nodes 1 --replication-factor 1
```

### Options

```
      --num-nodes int                   [Optional] Number of nodes in the cluster.
      --replication-factor int          [Optional] Replication factor of the cluster.
      --add-regions string              [Optional] Add regions for the nodes of the cluster to be placed in. Provide comma-separated strings in the following format: "--regions 'region-1-for-rr-cluster,region-2-for-rr-cluster'"
      --remove-regions string           [Optional] Remove regions from the cluster. Provide comma-separated strings in the following format: "--regions 'region-1-for-rr-cluster,region-2-for-rr-cluster'"
      --add-zones stringArray           [Optional] Add zones for the nodes of the cluster to be placed in. Provide the following double colon (::) separated fields as key-value pairs: "--add-zones 'zone-name=<zone1>::region-name=<region1>::num-nodes=<number-of-nodes-to-be-placed-in-zone>" Each zone must have the region and number of nodes to be placed in that zone. Add the region via --add-regions flag if not present in the universe. Each zone needs to be added using a separate --add-zones flag.
      --edit-zones stringArray          [Optional] Edit number of nodes in the zone for the cluster. Provide the following double colon (::) separated fields as key-value pairs: "--edit-zones 'zone-name=<zone1>::region-name=<region1>::num-nodes=<number-of-nodes-to-be-placed-in-zone>" Each zone must have the region and number of nodes to be placed in that zone. Each zone needs to be edited using a separate --edit-zones flag.
      --remove-zones stringArray        [Optional] Remove zones from the cluster. Provide the following double colon (::) separated fields as key-value pairs: "--remove-zones 'zone-name=<zone1>::region-name=<region1>" Each zone must have the region mentioned. Each zone needs to be removed using a separate --remove-zones flag.
      --instance-type string            [Optional] Instance Type for the universe nodes.
      --num-volumes int                 [Optional] Number of volumes to be mounted on this instance at the default path. Editing number of volumes is allowed only while changing instance-types.
      --volume-size int                 [Optional] The size of each volume in each instance. Editing volume size is allowed only while changing instance-types.
      --storage-type string             [Optional] Storage type used for this instance.
      --storage-class string            [Optional] Storage classs used for this instance, supported for Kubernetes.
      --add-user-tags stringToString    [Optional] Add User Tags to the DB instances. Provide as key=value pairs per flag. Example "--user-tags name=test --user-tags owner=development" OR "--user-tags name=test,owner=development". (default [])
      --edit-user-tags stringToString   [Optional] Edit existing User Tags in the DB instances. Provide as key=value pairs per flag. Example "--user-tags name=test --user-tags owner=development" OR "--user-tags name=test,owner=development". (default [])
      --remove-user-tags string         [Optional] Remove User tags from existing list in the DB instances. Provide comma-separated values in the following format: "--remove-user-tags user-tag-key-1,user-tag-key-2".
  -h, --help                            help for read-replica
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe to be edited.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe edit cluster](yba_universe_edit_cluster.md)	 - Edit clusters in a YugabyteDB Anywhere universe

