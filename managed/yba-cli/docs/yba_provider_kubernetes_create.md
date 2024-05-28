## yba provider kubernetes create

Create a Kubernetes YugabyteDB Anywhere provider

### Synopsis

Create a Kubernetes provider in YugabyteDB Anywhere

```
yba provider kubernetes create [flags]
```

### Examples

```
yba provider k8s create -n <provider-name> --type gke \
	--pull-secret-file <pull-secret-file-path> \
	--region region-name=us-west1 \
	--zone zone-name=us-west1-b,region-name=us-west1,storage-class=<storage-class>,\
	overrirdes-file-path=<overrirdes-file-path> \
	--zone zone-name=us-west1-a,region-name=us-west1,storage-class=<storage-class> \
	--zone zone-name=us-west1-c,region-name=us-west1,storage-class=<storage-class>
```

### Options

```
      --type string               [Required] Kubernetes cloud type. Allowed values: aks, eks, gke, custom.
      --image-registry string     [Optional] Kubernetes Image Registry. (default "quay.io/yugabyte/yugabyte")
      --pull-secret-file string   [Required] Kuberenetes Pull Secret File Path.
      --kubeconfig-file string    [Optional] Kuberenetes Config File Path.
      --storage-class string      [Optional] Kubernetes Storage Class.
      --region stringArray        [Required] Region associated with the Kubernetes provider. Minimum number of required regions = 1. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,config-file-path=<path-for-the-kubernetes-region-config-file>,storage-class=<storage-class>,cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,pod-address-template=<pod-address-template>,overrides-file-path=<path-for-file-contanining-overrides>". Region name is a required key-value. Config File Path, Storage Class, Cert Manager Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and Overrides File Path are optional. Each region needs to be added using a separate --region flag.
      --zone stringArray          [Required] Zone associated to the Kubernetes Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>,config-file-path=<path-for-the-kubernetes-region-config-file>,storage-class=<storage-class>,cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,pod-address-template=<pod-address-template>,overrides-file-path=<path-for-file-contanining-overrides>". Zone name and Region name are required values.  Config File Path, Storage Class, Cert Manager Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and Overrides File Path are optional. Each --region definition must have atleast one corresponding --zone definition. Multiple --zone definitions can be provided per region.Each zone needs to be added using a separate --zone flag.
      --airgap-install            [Optional] Do YugabyteDB nodes have access to public internet to download packages.
  -h, --help                      help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider kubernetes](yba_provider_kubernetes.md)	 - Manage a YugabyteDB Anywhere K8s provider

