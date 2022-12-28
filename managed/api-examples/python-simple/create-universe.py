import http.client
import json
import uuid
from pprint import pprint

# platform_address = <TO BE SET>
# ex: platform_address = "127.0.0.1:9000"
platform_address = "127.0.0.1:9000"

# platform_api_key = <TO BE SET>
# ex: platform_api_key="b588a9c9-fe8d-46f2-b946-9b39a31e2cc8"
platform_api_key = "42dcb1a4-430f-4419-8033-cb1338518c35"


conn = http.client.HTTPConnection(f"{platform_address}")

headers = {
  'Content-Type': "application/json",
  'X-AUTH-YW-API-TOKEN': f"{platform_api_key}"
}

# To identify the customer UUID for this API call, see the "Get Session Info" section at
# https://github.com/yugabyte/yugabyte-db/blob/master/managed/api-examples/python-simple/create-universe.ipynb

customer_uuid = "f33e3c9b-75ab-4c30-80ad-cba85646ea39"

# Provider is GCP. Storage type and instance types are defined for GCP.
provider_uuid = "797c1119-8683-46ea-8e2f-5b7757769c8d"

universe_name = "nsingh-test-universe1"

num_nodes = 3

replication_factor = 3

instance_type = "n1-standard-1"

# Make API call to get the provider config.
conn.request(
  "GET",
  f"/api/v1/customers/{customer_uuid}/providers/{provider_uuid}",
  headers=headers
).getresponse()

res = conn.getresponse()
provider_data = json.load(res)
pprint(provider_data)
print("-----\n\n\n")

# Make API call to get the access key associated with the provider.
conn.request(
  "GET",
  f"/api/v1/customers/{customer_uuid}/providers/{provider_uuid}/access_keys",
  headers=headers
)
res = conn.getresponse()
access_keys = json.load(res)
access_key_code = access_keys[0]["idKey"]["keyCode"]
pprint(access_key_code)
print("-----\n\n\n")

# Get regions and zones from the provider.
regions = provider_data["regions"]
region_1 = regions[0]
region_1_zones = region_1["zones"]

# Create the user intent using the above information.
# Additional config changes can be made in the user intent.
user_intent = {
    "regionList": [
        region_1["uuid"]
    ],
    "universeName": universe_name,
    "provider": provider_uuid,
    "assignPublicIP": True,
    "useTimeSync": True,
    "enableYSQL": True,
    "enableYSQLAuth": True,
    "ysqlPassword": "Admin@123",
    "enableYCQL": True,
    "enableYCQLAuth": True,
    "ycqlPassword": "Admin@123",
    "enableYEDIS": True,
    "enableNodeToNodeEncrypt": True,
    "enableClientToNodeEncrypt": True,
    "enableIPV6": False,
    "enableExposingService": "UNEXPOSED",
    "awsArnString": "",
    "providerType": provider_data["code"],
    "instanceType": instance_type,
    "numNodes": num_nodes,
    "accessKeyCode": access_key_code,
    "replicationFactor": num_nodes,
    "ybSoftwareVersion": "2.16.1.0-b13",
    "useSystemd": True,
    "dedicatedNodes": False,
    "deviceInfo": {
        "volumeSize": 375,
        "numVolumes": 1,
        "diskIops": 3000,
        "throughput": 125,
        "storageType": "Persistent",
        "storageClass": "standard"
    },
    "masterGFlags": [],
    "tserverGFlags": [],
    "instanceTags": []
}

az_idx = 0
placement_az_list = []
# AZ UUID to number of nodes and replication (data copy in the AZ) dictionary.
az_num_nodes_info = {}
replication_factor_allotment = replication_factor

# Simple round robin allotment of nodes to the AZs in the region.
# This can be changed to get the desired allotment of nodes to the AZs.
for idx in range(num_nodes):
    az_idx = az_idx + 1 if az_idx < len(region_1_zones) - 1 else 0
    selected_zone = region_1_zones[az_idx]
    num_nodes_info = az_num_nodes_info.get(selected_zone["uuid"], {})
    if replication_factor_allotment > 0:
        num_nodes_info["replicationFactor"] = num_nodes_info.get("replicationFactor", 0) + 1
    num_nodes_info["numNodesInAZ"] = num_nodes_info.get("numNodesInAZ", 0) + 1
    placement_az_list.append({
        "uuid": selected_zone["uuid"],
        "name": selected_zone["name"],
        "subnet": selected_zone["subnet"],
        "replicationFactor": num_nodes_info["replicationFactor"],
        "numNodesInAZ": num_nodes_info["numNodesInAZ"],
        "isAffinitized": replication_factor_allotment > 0
    })
    az_num_nodes_info[selected_zone["uuid"]] = num_nodes_info
    replication_factor_allotment = replication_factor_allotment - 1

# Placement info created using the region and AZ for the provider to place the nodes
# in different AZs.
placement_info = {
    "cloudList": [
        {
            "uuid": user_intent["provider"],
            "code": user_intent["providerType"],
            "regionList": [
                {
                    "uuid": region_1["uuid"],
                    "code": region_1["code"],
                    "name": region_1["name"],
                    "azList": placement_az_list
                }
            ]
        }
    ]
}

# Default communication ports.
communication_ports = {
    "masterHttpPort": 7000,
    "masterRpcPort": 7100,
    "tserverHttpPort": 9000,
    "tserverRpcPort": 9100,
    "redisServerHttpPort": 11000,
    "redisServerRpcPort": 6379,
    "ybControllerHttpPort": 14000,
    "ybControllerRpcPort": 18018,
    "yqlServerHttpPort": 12000,
    "yqlServerRpcPort": 9042,
    "ysqlServerHttpPort": 13000,
    "ysqlServerRpcPort": 5433,
    "nodeExporterPort": 9300
}

# Cluster object.
clusters = [
    {
        "uuid": str(uuid.uuid4()),
        "clusterType": "PRIMARY",
        "regions": regions,
        "placementInfo": placement_info,
        "userIntent": user_intent,
    }
]

primary_cluster = clusters[0]

node_details_common = {
    "disksAreMountedByUUID": True,
    "state": "ToBeAdded",
}

node_details_common.update(communication_ports)

pprint(az_num_nodes_info)
print("-----\n\n\n")

# Prepare the node details using the number of nodes in each AZ.
node_details_set = []
for idx in range(num_nodes):
    node_idx = idx + 1
    az_uuid = next(iter(az_num_nodes_info))
    num_nodes_info = az_num_nodes_info[az_uuid]
    node_details_set.append({
        "nodeIdx": node_idx,
        "placementUuid": primary_cluster["uuid"],
        "azUuid": az_uuid,
        "cloudInfo": {
            "instance_type": user_intent["instanceType"],
            "subnet_id": selected_zone["subnet"],
            "az": selected_zone["code"],
            "region": region_1["code"],
            "cloud": user_intent["providerType"],
            "assignPublicIP": True,
            "useTimeSync": True,
            "lun_indexes": []
        }
    })
    remaining_num_nodes = num_nodes_info.get("numNodesInAZ", 0) - 1
    if remaining_num_nodes <= 0:
        # Alloted number of nodes exhausted.
        # Delete the mapping.
        del az_num_nodes_info[az_uuid]
    else:
        # Update the remaining number of nodes in the current AZ.
        num_nodes_info["numNodesInAZ"] = remaining_num_nodes
        az_num_nodes_info[az_uuid] = num_nodes_info

# Update the common node details information.
for node_details in node_details_set:
    node_details.update(node_details_common)

# Prepare the create universe param.
create_universe_param = {
   "nodeExporterUser": "prometheus",
   "enableYbc": False,
   "ybcSoftwareVersion": "",
   "installYbc": False,
   "ybcInstalled": False,
   "expectedUniverseVersion": -1,
   "encryptionAtRestConfig": {
      "key_op": "UNDEFINED"
   },
   "nodeDetailsSet": node_details_set,
   "communicationPorts": communication_ports,
   "extraDependencies": {
      "installNodeExporter": True
   },
   "firstTry": True,
   "clusters": clusters,
   "currentClusterType": "PRIMARY",
   "nodePrefix": "yb-admin-{}".format(user_intent["universeName"]),
   "nextClusterIndex": 1,
   "allowInsecure": True,
   "importedState": "NONE",
   "capability": "EDITS_ALLOWED",
   "updateOptions": [
      "UPDATE"
   ],
   "clusterOperation": "CREATE",
   "xclusterInfo": {
      "targetXClusterConfigs": [],
      "sourceXClusterConfigs": []
   },
   "targetXClusterConfigs": [],
   "sourceXClusterConfigs": []
}

# Make API call to create the universe.
conn.request(
  "POST",
  f"/api/v1/customers/{customer_uuid}/universes/clusters",
  body=json.dumps(create_universe_param),
  headers=headers
)

res = conn.getresponse()
pprint(json.load(res))

# This response includes a task UUID that represents an asynchronous operation.
# It returns a payload like
# {'resourceUUID': 'eb6af755-b7b0-401c-a4f0-43f1fab40c76',
# 'taskUUID': '9d509ffc-7a27-4142-87df-e481e82dd518'}
