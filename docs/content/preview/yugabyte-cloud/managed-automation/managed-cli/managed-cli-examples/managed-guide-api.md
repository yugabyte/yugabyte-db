---
private: true
---
<!---
title: Guide for ybm API automation
headerTitle: "Tutorial: Create a cluster using the REST API"
linkTitle: "Tutorial: REST API"
description: Tutorial for using YugabyteDB Aeon REST API to create clusters.
headcontent: Example workflows for automation tools
menu:
  preview_yugabyte-cloud:
    identifier: managed-guide-api
    parent: managed-cli-examples
    weight: 60
type: docs
--->

The following tutorial shows how you can use the REST API to create clusters in YugabyteDB Aeon.

For documentation and Postman collection, refer to [YugabyteDB Aeon REST API](https://api-docs.yugabyte.com/docs/managed-apis/9u5yqnccbe8lk-yugabyte-db-managed-rest-api).

## Prerequisites

This guide assumes you have already done the following:

- Created and saved an [API key](../../managed-apikeys/).
- Obtained your [account ID and project ID](../../#account-details).
- Installed [jq](https://stedolan.github.io/jq/).

For convenience, add the API key and IDs to environment variables, as follows:

```sh
YBM_API_KEY="<api_key>"
YBM_ACCOUNT_ID="<account_ID>"
YBM_PROJECT_ID="<project_ID>"
```

To create VPCs and dedicated clusters, you need to [choose a plan](https://www.yugabyte.com/pricing/), or you can [start a free trial](../../../managed-freetrial/).

Note that you can only create one Sandbox cluster per account.

## Create a sandbox cluster

To create your free [sandbox](../../../cloud-basics/create-clusters/create-clusters-free/) cluster, you'll first set up environment variables for the version of YugabyteDB to use, and an IP allow list.

### Determine the database version

YugabyteDB Aeon has [four release tracks](../../../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on), Early Access, Innovation, or Preview for Sandbox clusters, and Innovation, Production, or Early Access for dedicated clusters.

To get the ID for the Preview track and add it to an environment variable, enter the following commands:

```sh
YBM_SOFTWARE="Preview"

YBM_SOFTWARE_TRACK_ID=$(
curl -s --request GET \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/software-tracks \
  --header "Authorization: Bearer $YBM_API_KEY" \
  --header 'Content-Type: application/json' | \
  tee /dev/stderr |
  jq -r '.data[] | select(.spec.name=="'$YBM_SOFTWARE'") | .info.id '
)
echo ; set | grep ^YBM_ | cut -c1-80
```

### Create an IP allow list

To access the cluster from your computer, you need to add your public IP to an IP allow list. The following creates an allow list with the address obtained from <http://ifconfig.me>, and puts the ID of the allow list into an environment variable:

```sh
YBM_ALLOW_LIST="home"

curl -s --request POST \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/projects/$YBM_PROJECT_ID/allow-lists  \
  --header "Authorization: Bearer $YBM_API_KEY" \
  --header 'Content-Type: application/json' \
  --data '{
  "name": "'${YBM_ALLOW_LIST}'",
  "description": "Gathered from http://ifconfig.me",
  "allow_list": [
    "'$(curl -s ifconfig.me)'/32"
  ]
}'

YBM_ALLOW_LIST_ID=$(
curl -s --request GET \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/projects/$YBM_PROJECT_ID/allow-lists \
  --header "Authorization: Bearer $YBM_API_KEY" \
  --header 'Content-Type: application/json' |
  tee /dev/stderr |
  jq -r '.data[] | select(.spec.name=="'${YBM_ALLOW_LIST}'") | .info.id'
)
echo ; set | grep ^YBM_ | cut -c1-80
```

### Deploy the cluster

Now you have all the information you need to deploy a cluster as environment variables:

```output.sh
YBM_ACCOUNT_ID=a1fcd466-f30a-4074-b042-[...]
YBM_ALLOW_LIST=home
YBM_ALLOW_LIST_ID=0cf48f5f-4dfe-4d71-a506-d6415fed6f8a
YBM_API_KEY=eyJhbGc[...]
YBM_CLUSTER_ID=ca209cb4-3714-4968-8800-[...]
YBM_PROJECT_ID=cd15fb18-3117-47d2-b705-[...]
YBM_SOFTWARE=Preview
YBM_SOFTWARE_TRACK_ID=250ef2a0-0555-4e3a-9418-00e6bdfe1cb0
```

The following defines a name for the cluster and a password for the database admin user. Then it calls the cluster creation API with one node in Ireland AWS region eu-west-1; the `num_cores`, `memory_mb`, and `disk_size_gb` settings are those of the free tier on AWS:

```sh
YBM_CLUSTER="my-free-yugabytedb"
YBM_ADMIN_PASSWORD="Password-5up3r53cr3t"

curl -s --request POST \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/projects/$YBM_PROJECT_ID/clusters \
  --header "Authorization: Bearer $YBM_API_KEY" \
  --header 'Content-Type: application/json' \
  --data '{
  "cluster_spec": {
    "name": "'$YBM_CLUSTER'",
    "cloud_info": {
      "code": "AWS",
      "region": "eu-west-1"
    },
    "cluster_info": {
      "cluster_tier": "FREE",
      "cluster_type": "SYNCHRONOUS",
      "num_nodes": 1,
      "fault_tolerance": "NONE",
      "node_info": {
        "num_cores": 2,
        "memory_mb": 4096,
        "disk_size_gb": 10
      },
      "is_production": false,
      "version": null
    },
    "network_info": {
      "single_tenant_vpc_id": null
    },
    "software_info": {
      "track_id": "'$YBM_SOFTWARE_TRACK_ID'"
    },
    "cluster_region_info": [
      {
        "placement_info": {
          "cloud_info": {
            "code": "AWS",
            "region": "eu-west-1"
          },
          "num_nodes": 1,
          "vpc_id": null,
          "num_replicas": 1,
          "multi_zone": false
        },
        "is_default": true,
        "is_affinitized": true,
        "accessibility_types": [
          "PUBLIC"
        ]
      }
    ]
  },
  "allow_list_info": [ "'"$YBM_ALLOW_LIST_ID"'" ],
  "db_credentials": {
    "ycql": {
      "username": "admin",
      "password": "'"$YBM_ADMIN_PASSWORD"'"
    },
    "ysql": {
      "username": "admin",
      "password": "'"$YBM_ADMIN_PASSWORD"'"
    }
  }
}'
```

The cluster takes five to ten minutes to deploy.

## List your clusters

Enter the following to check the progress from the list of clusters:

```sh
curl -s --request GET \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/projects/$YBM_PROJECT_ID/clusters \
  --header "Authorization: Bearer $YBM_API_KEY" \
  --header 'Content-Type: application/json' \
  | jq -r '.data[] | .info.id +" "+ .spec.name +": "+ .info.state'
```

```output
ca209cb4-3714-4968-8800-4db1b551744a my-free-yugabytedb: BOOTSTRAPPING
```

Set an environment variable for the ID of the cluster currently being deployed:

```sh
YBM_CLUSTER_ID=$(
curl -s --request GET \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/projects/$YBM_PROJECT_ID/clusters \
  --header "Authorization: Bearer $YBM_API_KEY" \
  --header 'Content-Type: application/json' |
  tee /dev/stderr |
  jq -r '.data[] | select(.spec.name=="'$YBM_CLUSTER'") | .info.id'
)
echo ; set | grep ^YBM_ | cut -c1-80
```

You can use the cluster ID to monitor the progress of your cluster deployment. Enter the following:

```sh
until curl -s --request GET \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/projects/$YBM_PROJECT_ID/clusters/$YBM_CLUSTER_ID   --header "Authorization: Bearer $YBM_API_KEY" | \
 grep '"state":"ACTIVE"' ; do sleep 1 ; done
```

This loop stops when the cluster is ready.

## Connect to the database

To be able to connect to the default database `yugabyte` using the database admin user you created, you need to obtain the public host address of the cluster from the list of endpoints.

The following commands save the connection parameters you need to connect to a YugabyteDB Aeon database to environment variables:

```sh
PGDATABASE=yugabyte
PGUSER="admin"
PGPASSWORD="$YBM_ADMIN_PASSWORD"
PGPORT="5433"
PGSSLMODE=require

PGHOST=$(
curl -s --request GET \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/projects/$YBM_PROJECT_ID/clusters \
  --header "Authorization: Bearer $YBM_API_KEY" \
  --header 'Content-Type: application/json' |
  tee /dev/stderr |
  jq -r '.data[] | select(.spec.name=="'$YBM_CLUSTER'") | .info.cluster_endpoints[0].host '
)
echo ; set | grep ^PG
export PGDATABASE PGUSER PGHOST PGPORT PGPASSWORD PGSSLMODE
```

You can now use any PostgreSQL tool or application to [connect to the database](../../../cloud-connect/connect-client-shell/).

## Terminate the cluster

You can pause a cluster when you don't need it:

```sh
curl -s --request POST \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/projects/$YBM_PROJECT_ID/clusters/$YBM_CLUSTER_ID/pause \
  --header "Authorization: Bearer $YBM_API_KEY" --data ''
```

(Note that the Sandbox is free, and can't be paused.)

If you don't need a cluster anymore, you can terminate the service:

```sh
curl -s --request DELETE \
  --url https://cloud.yugabyte.com/api/public/v1/accounts/$YBM_ACCOUNT_ID/projects/$YBM_PROJECT_ID/clusters/$YBM_CLUSTER_ID \
  --header "Authorization: Bearer $YBM_API_KEY"
```
