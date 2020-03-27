# YugabyteDB Operator Design

A cluster of YugabyteDB should be created using the `ybclusters.yugabyte.com` custom resource definition. Below are sample custom resource specs for creating 3-master, 3 t-servers cluster using the CRD. The [minimal spec](#Minimal-Spec-Sample) and the [full spec](#Full-Spec-Sample) are essentially same, with [minimal spec](#Minimal-Spec-Sample) stripping away all the default vaules. The samples are followed by an explanation of different configuration options available on the YugabyteDB CRD.


## Minimal Spec Sample

Below is an example of a spec with minimum attributes required to define a custom resorce of type `ybclusters.yugabyte.com`. The spec below uses all possible defaults & specifies only the mandatory attributes.

```yaml
apiVersion: yugabyte.com/v1alpha1
kind: YBCluster
metadata:
  name: yugabytedb
spec:
  replicationFactor: 3
  master:
    replicas: 3
    storage:
      size: 10Gi
  tserver:
    replicas: 3
    storage:
      count: 1
      size: 10Gi
```

The spec can be used as a starting point &  can be updated with any other attributes, as needed. All valid attributes & their description is as below.

## Full Spec Sample

Below is an example of the full spec for custom resource `ybclusters.yugabyte.com`. It shows all possible attributes that can be specified with it, along with the default value, if any.

```yaml
apiVersion: yugabyte.com/v1alpha1
kind: YBCluster
metadata:
  name: yugabytedb
  namespace: yugabytedb
spec:
  image:
    repository: yugabytedb/yugabyte     # Optional. Default value "yugabytedb/yugabyte".
    tag: 1.3.2.0-b19                    # Optional. Default value "1.3.2.0-b19".
    pullPolicy: IfNotPresent            # Optional. Default value "IfNotPresent".
  tls:
    enabled: false                      # Optional. Default value "false".
    # rootCA:
      # cert:                           # Uncomment if tls.enabled is set to true. Specify the root certificate generated .
      # key:                            # Uncomment if tls.enabled is set to true. Specify the root certificate key generated.
  replicationFactor: 3                # Required. Data replication factor for the cluster. Should be >= 1.
  master:
    replicas: 3                         # Required. Pod replica count for Master.
    # Mentioning network ports is optional. If some or all ports are not specified, then they will be defaulted to below-mentioned values.
    masterUIPort: 7000                  # Optional. Default value "7000".
    masterRPCPort: 7100                 # Optional. Default value "7100".
    enableLoadBalancer: false           # Optional. Default value "false". Change it to true to be able to access YugabyteDB Master UI over the internet.
    podManagementPolicy: Parallel       # Optional. Default value "Parallel", out of valid values "Parallel" and "OrderedReady". If "OrderedReady" value is specified, cluster will take more time coming up.
    storage:
      count: 1                          # Optional. Default value "1".
      size: 10Gi
      storageClass: standard            # Optional. Default value "standard". Provide storage class to use.
    resources:                          # Optional. No resource requests/limits will be applied, if this property is omitted. You may also specify one or both of requests & limits
      requests:
        cpu: 2
        memory: 1Gi
      limits:
        cpu: 2
        memory: 1Gi
    gflags:                             # Optional. No GFlags will be applied, if this property is omitted. List at least one, when specified.
      - key: default_memory_limit_to_ram_ratio
        value: 0.85
  tserver:
    replicas: 3                         # Required. Pod replica count for TServer.
    # Mentioning network ports is optional. If some or all ports are not specified, then they will be defaulted to below-mentioned values, except for tserver-ui.
    # For tserver-ui a cluster ip service will be created if the yb-tserver-ui port is explicitly mentioned. If it is not specified, only StatefulSet & headless service will be created for TServer. TServer ClusterIP service creation will be skipped. Whereas for Master, all 3 kubernetes objects will always be created.
    tserverUIPort: 9000                 # Optional. ClusterIP service will not be created, if omitted.
    tserverRPCPort: 9100                # Optional. No default value. ClusterIP service won't be created for tserver, if this is omitted.
    ycqlPort: 9042                      # Optional. Default value "9042".
    yedisPort: 6379                     # Optional. Default value "6379".
    ysqlPort: 5433                      # Optional. Default value "5433".
    enableLoadBalancer: false           # Optional. Default value "false". Change it to true to be able to access YugabyteDB TServer UI over the internet. Value will be ignored if "tserverUIPort" is omitted.
    # Volume claim template for TServer
    podManagementPolicy: Parallel       # Optional. Default value "Parallel", out of valid values "Parallel" and "OrderedReady". If "OrderedReady" value is specified, cluster will take more time coming up.
    storage:
      count: 1
      size: 10Gi
      storageClass: standard            # Optional. Provide storage class to use. Field will be defaulted to "standard", if omitted.
    resources:                          # Optional. No resource requests/limits will be applied, if this property is omitted. You may also specify one or both of requests & limits
      requests:
        cpu: 2
        memory: 1Gi
      limits:
        cpu: 2
        memory: 1Gi
    gflags:                             # Optional. No GFlags will be applied, if this property is omitted. List at least one, when specified.
      - key: default_memory_limit_to_ram_ratio
        value: 0.85
```

## Cluster Settings

### Image
Mention YugabyteDB docker image attributes such as `repository`, `tag` and `pullPolicy` under `image`.

### TLS
Enable TLS encryption for YugabyteDB, if desired. Default is disabled. You can use the TLS encryption with 3 GFlags, explained later. If you have set `enabled` to true, then you need to generate root certificate and key. Specify the two under `rootCA.cert` & `rootCA.key`. Refer [YugabytedB docs](https://docs.yugabyte.com/latest/secure/tls-encryption/prepare-nodes/#create-the-openssl-ca-configuration) (till [generate root configuration](https://docs.yugabyte.com/latest/secure/tls-encryption/prepare-nodes/#generate-root-configuration) section) for an idea on how to generate the certificate & key files.

### Replication Factor
Specify the required data replication factor. This is a **required** field.

### Master/TServer
Master & TServer are two essential components of a YugabyteDB cluster. Master is responsible for recording and maintaining system metadata & for admin activities. TServers are mainly responsible for data I/O.
Specify Master/TServer specific attributes under `master`/`tserver`. The valid attributes are as described below. These two are **required** fields.

#### Replicas
Specify count of pods for `master` & `tserver` under `replicas` field. This is a **required** field.

#### Network Ports
Control network configuration for Master & TServer, each of which support only a selected port attributes. Below table depicts the supported port attributes.
Note that these are **optional** fields, except `tserver.tserverUIPort`, hence below table also mentions default values for each port. Default network configuration will be used, if any or all of the acceptable fields are absent.

A ClusterIP service will be created when `tserver.tserverUIPort` port is specified. If it is not specified, only StatefulSet & headless service will be created for TServer. ClusterIP service creation will be skipped. Whereas for Master, all 3 kubernetes objects will always be created.

If `master.enableLoadBalancer` is set to `true`, then master UI service will be of type `LoadBalancer`. TServer UI service will be of type `LoadBalancer`, if `tserver.tserverUIPort` is specified and `tserver.enableLoadBalancer` is set to `true`. `tserver.enableLoadBalancer` will be ignored if `tserver.tserverUIPort` is not specified.

Table depicting acceptable port names, applicable component (Master/TServer) and port default values:

| Attribute      | Component | Default Value |
| -------------- | --------- | ------------- |
| masterUIPort   | Master    | 7000          |
| masterRPCPort  | Master    | 7100          |
| tserverUIPort  | TServer   | NA            |
| tserverRPCPort | TServer   | 9100          |
| ycqlPort       | TServer   | 9042          |
| yedisPort      | TServer   | 6379          |
| ysqlPort       | TServer   | 5433          |

#### podManagementPolicy
Specify pod management policy for statefulsets created as part of YugabyteDB cluster. Valid values are `Parallel` & `OrderedReady`, `Parallel` being the default value.

#### storage
Specify storage configurations viz. Storage `count`, `size` & `storageClass` of volumes. Typically 1 volume per Master instance is sufficient, hence Master has a default storage count of `1`. If storage class isn't specified, it will be defaulted to `standard`. Make sure kubernetes admin has defined `standard` storage class, before leaving this field out.

#### resources
Specify resource `requests` & `limits` under `resources` attribute. The resources to be specified are `cpu` & `memory`. The `resource` property in itself is optional & it won't be applied to created `StatefulSets`, if omitted. You may also choose to specify either `resource.requests` or `resource.limits` or both.

#### gflags
Specify list of GFlags for additional control on YugabyteDB cluster. Refer [Master Config Flags](https://docs.yugabyte.com/latest/admin/yb-master/#config-flags) & [TServer Config Flags](https://docs.yugabyte.com/latest/admin/yb-tserver/#config-flags) for list of supported flags.

If you have enabled TLS encryption, then you can set:
- `use_node_to_node_encryption` flag to enable node to node encryption
- `allow_insecure_connections` flag to specify if insecure connections are allowed when tls is enabled
- `use_client_to_server_encryption` flag to enable client to node encryption
