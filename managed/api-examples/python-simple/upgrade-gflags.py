import os
import http.client
import json
from pprint import pprint

# platform_address = <TO BE SET>
# ex: platform_address = "127.0.0.1:9000"
platform_address = "127.0.0.1:9000"

# platform_api_key = <TO BE SET>
# ex: platform_api_key="b588a9c9-fe8d-46f2-b946-9b39a31e2cc8"
platform_api_key = "b588a9c9-fe8d-46f2-b946-9b39a31e2cc8"


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
universe_uuid = "3ecfb8b3-ad6f-4d6e-90ed-80464d14fca7"

conn.request("GET", f"/api/v1/customers/{customer_uuid}/universes/{universe_uuid}", headers=headers)
univ_details = json.load(conn.getresponse())
pprint(univ_details)
print("-----\n\n\n")

# Prepare actual upgrade flags API request
upgrade_req = univ_details["universeDetails"]
upgrade_req["masterGFlags"] = {"vmodule": "secure1*"}
upgrade_req["tserverGFlags"] = {"vmodule": "secure2*"}
upgrade_req["upgradeOption"] = "Rolling"

# Make API call for upgrade flags
conn.request(
  "POST",
  f"/api/v1/customers/{customer_uuid}/universes/{universe_uuid}/upgrade/gflags",
  body=json.dumps(upgrade_req),
  headers=headers
)

res = conn.getresponse()
pprint(json.load(res))

# This response includes a task UUID that represents an asynchronous operation. In order to wait
# for this operation to complete, follow the example at
