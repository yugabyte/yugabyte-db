---
title: YugabyteDB ybm CLI quick examples
headerTitle: ybm CLI examples
linkTitle: Examples
description: Quick examples for using YugabyteDB Aeon ybm CLI.
headcontent: Quick examples for using ybm CLI
menu:
  stable_yugabyte-cloud:
    identifier: managed-cli-example-quick
    parent: managed-cli
    weight: 60
type: docs
---

The following sections provide quick examples for using [ybm CLI comands](https://github.com/yugabyte/ybm-cli/blob/main/docs/).

## API key

Create an API key in YugabyteDB Aeon:

```sh
ybm api-key create \
    --name developer \
    --duration 6 --unit Months \
    --description "Developer API key" \
    --role-name Developer
```

List API keys in YugabyteDB Aeon:

```sh
ybm api-key list
```

## Backups

### Backup policy

Modify the cluster backup policy:

```sh
ybm backup policy update \
    --cluster-name=test-cluster \
    --full-backup-frequency-in-days=2 \
    --retention-period-in-days=14
```

### Backup

Create a backup:

```sh
ybm backup create \
    --cluster-name=test-cluster \
    --retention-period=7
```

## Clusters

### Region

List AWS regions:

```sh
ybm region list \
  --cloud-provider AWS
```

List AWS instance types in us-west-2:

```sh
ybm region instance list \
  --cloud-provider AWS \
  --region us-west-2
```

### Cluster create

Create a local single-node cluster:

```sh
ybm cluster create \
  --cluster-name my-sandbox \
  --credentials username=admin,password=password123
```

Create a multi-node cluster:

```sh
ybm cluster create \
  --credentials username=admin,password=password \
  --cloud-provider AWS \
  --cluster-type SYNCHRONOUS \
  --region-info region=ap-northeast-1,num-nodes=3,num-cores=4,disk-size-gb=200  \
  --cluster-tier Dedicated \
  --fault-tolerance ZONE \
  --database-version Innovation \
  --cluster-name my-sandbox \
  --wait
```

### Cluster read replica create

Create a read-replica cluster:

```sh
ybm cluster read-replica create \
  --replica num-cores=2,\
  memory-mb=4096,\
  disk-size-gb=200,\
  cloud-provider=AWS,\
  region=us-west-3,\
  num-nodes=3,\
  vpc=my-vpc,\
  num-replicas=2,\
  multi-zone=true
```

### Usage

Output usage for clusters `my-cluster` and `your-cluster` for September 2023:

```sh
ybm usage get \
    --cluster-name my-cluster \
    --cluster-name your-cluster \
    --start 2023-09-01 \
    --end 2023-09-30
```

## Logging and integrations

### Integration

Create a configuration:

```sh
ybm integration create \
    --config-name datadog1 \
    --type DATADOG \
    --datadog-spec api-key=efXXXXXXXXXXXXXXXXXXXXXXXXXXXXee,site=US1
```

### Cluster database audit logging

Enable database audit logging for a cluster:

```sh
ybm cluster db-audit-logging enable \
  --cluster-name your-cluster \
  --integration-name datadog1 \
  --statement_classes="READ,WRITE,ROLE" \
  --wait \
  --ysql-config="log_catalog=true,log_client=true,log_level=NOTICE,log_relation=true,log_parameter=true,log_statement_once=true"
```

Disable database audit logging for a cluster.

```sh
ybm cluster db-audit-logging disable \
  --cluster-name your-cluster
```

Get information about database audit logging for a cluster.

```sh
ybm cluster db-audit-logging describe --cluster-name your-cluster
```

Update some fields of the log configuration.

```sh
ybm cluster db-audit-logging update \
  --cluster-name your-cluster \
  --integration-name your-integration \
  --statement_classes="WRITE,MISC" \
  --ysql-config="log_catalog=true,log_client=false,log_level=NOTICE,log_relation=false,log_parameter=true,log_statement_once=true"
```

### Database audit logging export

Assign a configuration to a cluster:

```sh
ybm db-audit-logs-exporter assign \
    --cluster-name my_cluster \
    --integration-name datadog1 \
    --statement_classes=READ,WRITE \
    --ysql-config==log_catalog=true,log_client=false,log_level=INFO,log_parameter=true
```

### Database query logging

Enable database query logging for a cluster:

```sh
ybm cluster db-query-logging enable \
--cluster-name your-cluster \
--integration-name your-integration \
--log-line-prefix "%m :%r :%u @ %d :[%p] : " \
--log-min-duration-statement -1 \
--log-connections false \
--log-duration false \
--log-error-verbosity DEFAULT \
--log-statement NONE
```

Disable database query logging for a cluster.

```sh
ybm cluster db-query-logging disable \
--cluster-name your-cluster
```

Get information about database query logging for a cluster.

```sh
ybm cluster db-query-logging describe --cluster-name your-cluster
```

Update some fields of the log configuration.

```sh
ybm cluster db-query-logging update \
--cluster-name "your-cluster" \
--integration-name your-integration \
--log-line-prefix "%m :%r :%u @ %d :[%p] :" \
--log-min-duration-statement 60
```

## Networking

### Network allow list

Create a single address allow list:

```sh
ybm network-allow-list create \
    --name=my-computer \
    --description="my IP address" \
    --ip-addr=$(curl ifconfig.me)
```

### Cluster network

Assign an allow list:

```sh
ybm cluster network allow-list assign \
  --cluster-name=<cluster_name> \
  --network-allow-list=<allow_list_name>
```

### VPC peering

Create a peering connection on GCP:

```sh
ybm vpc peering create \
  --name demo-peer \
  --yb-vpc-name demo-vpc \
  --cloud-provider GCP \
  --app-vpc-project-id project \
  --app-vpc-name application-vpc-name \
  --app-vpc-cidr 10.0.0.0/18
```

### VPC

Create a global VPC on GCP:

```sh
ybm vpc create \
    --name demo-vpc \
    --cloud-provider GCP \
    --global-cidr 10.0.0.0/18
```

## Users and roles

### Role

List roles in YugabyteDB Aeon:

```sh
ybm role list
```

Describe the Admin role:

```sh
ybm role describe --role-name admin
```

You can use this command to view all the available permissions.

Create a role:

```sh
ybm role create --role-name backuprole \
  --permissions resource-type=BACKUP,operation-group=CREATE \
  --permissions resource-type=BACKUP,operation-group=DELETE
```

### User

List users in YugabyteDB Aeon:

```sh
ybm user list
```

Invite a user to YugabyteDB Aeon:

```sh
ybm user invite --email developer@mycompany.com --role Developer
```
