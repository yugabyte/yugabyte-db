import http.client
import json
import sys
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

# To identify the universe uuid for a given universe, use the the example at
# https://github.com/yugabyte/yugabyte-db/blob/master/managed/api-examples/python-simple/list-universes.ipynb
# to list all universes and filter by name
# universe_uuid = <TO BE SET>
# ex: universe_uuid = "3ecfb8b3-ad6f-4d6e-90ed-80464d14fca7"
universe_uuid = "66b5adbd-1739-482f-9cba-46f4e69b3f52"

node_name = "yb-admin-nsingh-test-universe1-n2"

# "STOP", "HARD_REBOOT", "REBOOT", "REMOVE", "QUERY" etc.
node_action = "REBOOT"

# Make API call to get the universe.
conn.request("GET", f"/api/v1/customers/{customer_uuid}/universes/{universe_uuid}", headers=headers)
universe = json.load(conn.getresponse())
pprint(universe)
print("-----\n\n\n")

# Get the universe details json.
node_details_set = universe["universeDetails"]["nodeDetailsSet"]

node_found = False
allowed_actions = []
node_names = []
for node_details in node_details_set:
    node_names.append(node_details.get("nodeName"))
    if node_name == node_details.get("nodeName"):
        allowed_actions = node_details.get("allowedActions")
        node_found = True
        break

if not node_found:
    err_msg = f"Node name {node_name} is not found in {node_names}"
    print(err_msg)
    sys.exit(err_msg)

if node_action not in allowed_actions:
    err_msg = f"Node action {node_action} is not found in {allowed_actions}"
    print(err_msg)
    sys.exit(err_msg)

# Prepare actual upgrade flags API request.
node_action_param = {
    "nodeAction": node_action
}

# Make API call to submit the node action.
conn.request(
  "PUT",
  f"/api/v1/customers/{customer_uuid}/universes/{universe_uuid}/nodes/{node_name}",
  body=json.dumps(node_action_param),
  headers=headers
)

res = conn.getresponse()
pprint(json.load(res))

# This response includes a task UUID that represents an asynchronous operation.
