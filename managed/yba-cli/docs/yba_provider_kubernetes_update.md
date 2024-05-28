## yba provider kubernetes update

Update a Kubernetes YugabyteDB Anywhere provider

### Synopsis

Update a Kubernetes provider in YugabyteDB Anywhere

```
yba provider kubernetes update [flags]
```

### Options

```
      --new-name string             [Optional] Updating provider name.
      --type string                 [Optional] Updating kubernetes cloud type. Allowed values: aks, eks, gke, custom.
      --image-registry string       [Optional] Updating kubernetes Image Registry.
      --pull-secret-file string     [Optional] Updating kuberenetes Pull Secret File Path.
      --kubeconfig-file string      [Optional] Updating kuberenetes Config File Path.
      --storage-class string        [Optional] Updating kubernetes Storage Class.
      --add-region stringArray      [Optional] Add region associated with the Kubernetes provider. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,config-file-path=<path-for-the-kubernetes-region-config-file>,storage-class=<storage-class>,cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,pod-address-template=<pod-address-template>,overrides-file-path=<path-for-file-contanining-overrides>". Region name is a required key-value. Config File Path, Storage Class, Cert Manager Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and Overrides File Path are optional. Each region needs to be added using a separate --add-region flag.
      --add-zone stringArray        [Optional] Add zone associated to the Kubernetes Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>,config-file-path=<path-for-the-kubernetes-region-config-file>,storage-class=<storage-class>,cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,pod-address-template=<pod-address-template>,overrides-file-path=<path-for-file-contanining-overrides>". Zone name and Region name are required values.  Config File Path, Storage Class, Cert Manager Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and Overrides File Path are optional. Each --add-region definition must have atleast one corresponding --add-zone definition. Multiple --add-zone definitions can be provided per region.Each zone needs to be added using a separate --add-zone flag.
      --edit-region stringArray     [Optional] Edit region associated with the Kubernetes provider. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,config-file-path=<path-for-the-kubernetes-region-config-file>,storage-class=<storage-class>,cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,pod-address-template=<pod-address-template>,overrides-file-path=<path-for-file-contanining-overrides>". Region name is a required key-value. Config File Path, Storage Class, Cert Manager Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and Overrides File Path are optional. Each region needs to be edited using a separate --edit-region flag.
      --edit-zone stringArray       [Optional] Edit zone associated to the Kubernetes Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>,config-file-path=<path-for-the-kubernetes-region-config-file>,storage-class=<storage-class>,cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,pod-address-template=<pod-address-template>,overrides-file-path=<path-for-file-contanining-overrides>". Zone name and Region name are required values. Config File Path, Storage Class, Cert Manager Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and Overrides File Path are optional. Each zone needs to be modified using a separate --edit-zone flag.
      --remove-region stringArray   [Optional] Region name to be removed from the provider. Each region to be removed needs to be provided using a separate --remove-region definition. Removing a region removes the corresponding zones.
      --remove-zone stringArray     [Optional] Remove zone associated to the Kubernetes Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>". Zone name, Region name are required values. Each zone needs to be removed using a separate --remove-zone flag.
      --airgap-install              [Optional] Do YugabyteDB nodes have access to public internet to download packages.
  -h, --help                        help for update
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

