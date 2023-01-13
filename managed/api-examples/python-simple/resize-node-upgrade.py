import json
import requests
from pprint import pprint

# ==================================================================================================
# --- Constants
# ==================================================================================================

# Base url endpoint to make requests to platform. Consists of "platform address + /api/v1"
BASE_URL = "http://localhost:9000/api/v1"

# To identify the customer UUID for this API call, see the "Get Session Info" section at
# https://github.com/yugabyte/yugabyte-db/blob/master/managed/api-examples/python-simple/
#   create-universe.ipynb
CUSTOMER_UUID = "f33e3c9b-75ab-4c30-80ad-cba85646ea39"

# To identify the universe uuid for a given universe, use the the example at
# https://github.com/yugabyte/yugabyte-db/blob/master/managed/api-examples/python-simple/
#   list-universes.ipynb
# to list all universes and filter by name
UNIVERSE_UUID = "98bc8527-88cf-407e-9fd6-a79f5cc7c9d6"

# Platform api key to be set
X_AUTH_YW_API_TOKEN = "72733523-878f-4d04-b63a-33f211bd9a3d"

DEFAULT_HEADERS = {"Content-Type": "application/json", "X-AUTH-YW-API-TOKEN": X_AUTH_YW_API_TOKEN}


GET_UNIVERSE_URL = BASE_URL + "/customers/{customer_uuid}/universes/{universe_uuid}"
POST_RESIZE_NODE_UPGRADE_URL = BASE_URL + \
    "/customers/{customer_uuid}/universes/{universe_uuid}/upgrade/resize_node"


# ==================================================================================================
# --- Api request
# ==================================================================================================

# Instance type changes
# <TO BE SET>
instance_type = "c5.2xlarge"

# Volume size changes in GB, must be greater than current volume
# <TO BE SET>
volume_size = None

# Get Universe Details
response = requests.get(GET_UNIVERSE_URL.format(universe_uuid=UNIVERSE_UUID,
                        customer_uuid=CUSTOMER_UUID), headers=DEFAULT_HEADERS)
response_json = response.json()
universe_details = response_json["universeDetails"]
pprint(universe_details)
print("-----\n\n\n")

univ_details_keys_keep = ["universeUUID", "nodePrefix", "clusters"]
new_universe_details = {k: universe_details[k] for k in univ_details_keys_keep}
new_universe_details["sleepAfterMasterRestartMillis"] = 0   # Default is 18000 ms, <TO BE SET>
new_universe_details["sleepAfterTServerRestartMillis"] = 0  # Default is 18000 ms, <TO BE SET>


if volume_size:
    print("Instance volume size before: ",
          new_universe_details["clusters"][0]["userIntent"]["deviceInfo"]["volumeSize"])
    print("Modifying volume size to:", volume_size)
    new_universe_details["clusters"][0]["userIntent"]["deviceInfo"]["volumeSize"] = volume_size


if instance_type:
    print("Instance type before: ",
          new_universe_details["clusters"][0]["userIntent"]["instanceType"])
    print("Modifying instance type to:", instance_type)
    new_universe_details["clusters"][0]["userIntent"]["instanceType"] = instance_type

print("-----\n\n\n")

# This response includes a task UUID that represents an asynchronous operation. In order to wait
# for this operation to complete
response = requests.post(POST_RESIZE_NODE_UPGRADE_URL
                         .format(universe_uuid=UNIVERSE_UUID, customer_uuid=CUSTOMER_UUID),
                         headers=DEFAULT_HEADERS, data=json.dumps(new_universe_details))
response_json = response.json()
print(response_json)
