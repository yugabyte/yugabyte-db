# Copyright (c) YugaByte, Inc.
#
# Copyright 2021 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import urllib.request
import json
from urllib.error import HTTPError


# def exception_handling():


def exception_handling(func):
    def inner_function(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except HTTPError as e:
            content = e.read().decode("utf-8")
            print (content)
            if "html>" in content:
                message = "Invalid YB_PLATFORM_URL URL or params, Getting html page in response"
                response = {"data": message, "status": "failed", "error": message}
                print(response)
            else:
                response = {"data": content, "status": "failed", "error": content}
                print(response)
        except Exception as e:
            response = {"data": str(e), "status": "failed", "error": str(e)}
            print(response)
    return inner_function

def call_api(url, auth_uuid, data=None, is_delete=False):
    """
    Call the corresponding url with auth token, headers and returns the response.
    :param url: url to be called.
    :param auth_uuid: authentication token of the customer.
    :param data: data for POST request.
    :param is_delete: to identify the delete call.
    :return: response of the API call.
    """
    if not is_delete:
        request = urllib.request.Request(url)
    else:
        request = urllib.request.Request(url, method="DELETE")

    request.add_header('X-AUTH-YW-API-TOKEN', auth_uuid)
    request.add_header('Content-Type', 'application/json; charset=utf-8')
    if data:
        response = urllib.request.urlopen(request, json.dumps(data).encode('utf-8'))
    else:
        response = urllib.request.urlopen(request)
    return response


def get_universe_details(base_url, customer_uuid, auth_uuid, universe_name, base_dir):
    """
    Get the universe details and store it in a json file after formatting the json.

    :param base_url: base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: authentication token for the customer.
    :param universe_name: Name of the universe to take a json data from.
    :param base_dir: base directory in which the json file should be stored.
    :return: None
    """
    universe = get_universe_by_name(base_url, customer_uuid, auth_uuid, universe_name)
    if universe:
        configure_json = create_universe_config(universe, universe["name"])
        file = base_dir + '/' + universe["name"] + ".json"
        with open(file, 'w') as file_obj:
            json.dump(configure_json, file_obj)
        response = {
            "data": "Detail of universe has been saved to " + str(file),
            "status": "success",
            "error": ""
        }
        handle_response(response)
    else:
        response = {
            "data": "Universe details not found",
            "status": "failed",
            "error": "Universe with " + universe_name + " is not found."
        }
        handle_response(response)


def get_universe_details_by_uuid(base_url, customer_uuid, auth_uuid, universe_uuid, base_dir):
    """
    Get universe details from UUID and store it in json after formatting it.

    :param base_url: base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: authentication token for the customer.
    :param universe_uuid: UUID of the universe to take a json data from.
    :param base_dir: base directory in which the json file should be stored.
    :return: None
    """
    universe = get_universe_by_uuid(base_url, customer_uuid, auth_uuid, universe_uuid)
    if universe:
        configure_json = create_universe_config(universe, universe["name"])
        file = base_dir + '/' + universe["name"] + ".json"
        with open(file, 'w') as file_obj:
            json.dump(configure_json, file_obj)
        response = {
            "data": "Detail of universe has been saved to " + str(file),
            "status": "success",
            "error": ""
        }
        handle_response(response)
    else:
        response = {
            "data": "Universe details not found",
            "status": "failed",
            "error": "Universe details not found"
        }
        handle_response(response)

@exception_handling
def create_universe_from_config(universe_config, base_url, customer_uuid, auth_uuid):
    """
    Create the universe from universe config data by calling universe POST API.

    :param universe_config: universe config data.
    :param base_url: base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: authentication token for the customer.
    :return: None
    """
    universe_create_url = base_url + "/api/v1/customers/" + customer_uuid + "/universes"
    response = call_api(universe_create_url, auth_uuid, universe_config)
    universe_json = json.loads(response.read())
    task_id = universe_json['taskUUID']
    response = {"data": task_id, "status": "success", "error": ""}
    print(response)


def create_universe(base_url, customer_uuid, auth_uuid, input_file, universe_name=""):
    """
    Create the universe using the json and provided universe name.

    :param base_url: base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: authentication token for the customer.
    :param input_file: Directory of the stored universe json data.
    :param universe_name: Name of the new universe to be created.
    :return: None
    """
    configure_json = modify_universe_config(input_file, universe_name)
    universe_config_json = post_universe_config(configure_json, base_url, customer_uuid, auth_uuid)
    # add the missing fields in the config json.
    universe_config_json["clusterOperation"] = "CREATE"
    universe_config_json["currentClusterType"] = "PRIMARY"
    # Call universe config api to get the update config data.
    universe_config_json = post_universe_config(
        universe_config_json,
        base_url,
        customer_uuid,
        auth_uuid
    )
    if universe_config_json:
        create_universe_from_config(universe_config_json, base_url, customer_uuid, auth_uuid)
    else:
        data = {
            "data": 'Unable to Configure Universe for Customer %s' % customer_uuid,
            "status": "failed",
            "error": 'Unable to Configure Universe for Customer %s' % customer_uuid
        }
        handle_response(data)


@exception_handling
def get_task_details(task_id, base_url, customer_uuid, auth_uuid):
    """
    Get details of the ongoing task.

    :param task_id: Task UUID.
    :param base_url: base url of the platform back end.
    :param customer_uuid: authentication token for the customer.
    :param auth_uuid: authentication token for the customer.
    :return: None
    """
    task_url = base_url + "/api/v1/customers/" + customer_uuid + "/tasks/" + task_id
    response = call_api(task_url, auth_uuid)
    universe_json = json.loads(response.read())
    if universe_json["status"] == "Running":
        response = {"data": int(universe_json["percent"]), "status": "success", "error": ""}
        print(response)
    elif universe_json["status"] == "Success":
        response = {"data": 100, "status": "success", "error": ""}
        print(response)
    elif universe_json["status"] == "Failure":
        content = {
            "message": "{0} failed".format(universe_json["title"]),
            "details": universe_json["details"]
        }
        response = {"data": content, "status": "success",
                    "error": content}
        print(response)


def create_universe_config(universe_data, universe_name):
    """
    Create the universe config data from the json file.

    :param universe_data: Stored universe data.
    :param universe_name: Name of the new universe to be created.
    :return: Configured universe json.
    """
    configure_json = {}
    clusters = universe_data['universeDetails']['clusters']
    user_az_selected = universe_data['universeDetails']['userAZSelected']

    excluded_keys = [
        "uuid",
        "storageType",
        "awsArnString",
        "useHostname",
        "preferredRegion",
        "regions",
        "index",
        "placementInfo"
    ]

    clusters_list = get_cluster_list(clusters, excluded_keys, universe_name)
    configure_json["clusters"] = clusters_list
    configure_json["clusterOperation"] = "CREATE"
    configure_json["userAZSelected"] = user_az_selected
    configure_json["currentClusterType"] = "PRIMARY"

    return configure_json


def get_cluster_list(clusters, excluded_keys, universe_name):
    """
    Helper method for creating universe config which returns modified cluster list.

    :param clusters: List of clusters.
    :param excluded_keys: Keys to be excluded
    :param universe_name: Name of the new universe to be created.
    :return: cluster list.
    """
    clusters_list = []
    for each_cluster in clusters:
        cluster = {}
        for key, val in each_cluster.items():
            if key not in excluded_keys:
                user_intent = {}
                if key == "userIntent":
                    if universe_name:
                        val["universeName"] = universe_name
                    for key1, val1 in val.items():
                        if key1 not in excluded_keys:
                            if key1 == "deviceInfo":
                                val1.pop("diskIops")
                                val1.pop("storageType")
                            user_intent[key1] = val1
                    cluster[key] = user_intent
                else:
                    cluster[key] = val
        clusters_list.append(cluster)
    return clusters_list


def modify_universe_config(file_name, universe_name=""):
    """
    Modify the universe json with new name.

    :param file_name: Name of the json file.
    :param universe_name: New universe name.
    :return: Modified universe config data.
    """
    with open(file_name) as f:
        data = json.load(f)
    clusters = data["clusters"]
    if universe_name:
        for each_cluster in clusters:
            each_cluster["userIntent"]["universeName"] = universe_name
        with open(file_name, 'w') as file_obj:
            json.dump(data, file_obj)
    return data


@exception_handling
def post_universe_config(configure_json, base_url, customer_uuid, auth_uuid):
    """
    Call the universe config URL with the updated data.

    :param configure_json: Universe config json.
    :param base_url: base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: authentication token for the customer.
    :return: None
    """
    universe_config_url = base_url + "/api/v1/customers/" + customer_uuid + "/universe_configure"
    response = call_api(universe_config_url, auth_uuid, configure_json)
    universe_config_json = json.loads(response.read())
    return universe_config_json


@exception_handling
def get_universe_by_name(base_url, customer_uuid, auth_uuid, universe_name):
    """
    Get universe data by name of the universe.

    :param base_url: base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: authentication token for the customer.
    :param universe_name: Universe name.
    :return: None
    """
    universe_url = base_url + "/api/v1/customers/" + customer_uuid + "/universes"
    response = call_api(universe_url, auth_uuid)
    data = json.load(response)
    for universe in data:
        if universe["name"] == universe_name:
            del universe['pricePerHour']
            return universe


@exception_handling
def get_universe_by_uuid(base_url, customer_uuid, auth_uuid, universe_uuid):
    """
    Get universe details by UUID of the universe.

    :param base_url: base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: authentication token for the customer.
    :param universe_uuid: UUID of the universe.
    :return: None
    """
    universe_config_url = base_url + "/api/v1/customers/" + customer_uuid + \
                          "/universes/" + universe_uuid
    response = call_api(universe_config_url, auth_uuid)
    return json.load(response)


def get_universe_uuid(base_url, customer_uuid, auth_uuid, universe_name):
    """
    Get the UUID of the universe.

    :param base_url: base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid:  authentication token for the customer.
    :param universe_name: Name of the universe.
    :return: None.
    """
    universe = get_universe_by_name(base_url, customer_uuid, auth_uuid, universe_name)
    if universe:
        response = {"data": universe["universeUUID"], "status": "success", "error": ""}
        print(response)
    else:
        message = "Universe with {0} not found".format(universe_name)
        response = {"data": message, "status": "failed", "error": message}
        print(response)


@exception_handling
def get_customer_uuid(base_url, auth_uuid):
    """
    Get customer UUID.

    :param base_url: base url of the platform back end.
    :param auth_uuid: authentication token for the customer.
    :return: None
    """
    customer_url = base_url + "/api/v1/customers"
    response = call_api(customer_url, auth_uuid)
    data = json.load(response)
    if (len(data) == 1):
        response = {"data": str(data[0]), "status": "success", "error": ""}
        print(response)
    else:
        response = {"data": data, "status": "failed", "error": "Please provide customer UUID"}
        print(response)


@exception_handling
def delete_universe_by_id(base_url, customer_uuid, auth_uuid, universe_uuid):
    """
    Delete the universe by providing UUID.

    :param base_url: base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: authentication token for the customer.
    :param universe_uuid: UUID of the universe to be deleted.
    :return:
    """
    universe_delete_url = base_url + "/api/v1/customers/" + customer_uuid + "/universes/" \
                          + universe_uuid + "?isForceDelete=true"

    response = call_api(universe_delete_url, auth_uuid, is_delete=True)
    universe_json = json.loads(response.read())
    task_id = universe_json['taskUUID']
    response = {"data": task_id, "status": "success", "error": ""}
    print(response)


@exception_handling
def get_provider_data(base_url, customer_uuid, auth_uuid):
    provider_url = base_url + "/api/v1/customers/" + customer_uuid + "/providers"
    response = call_api(provider_url, auth_uuid)
    provider_data = json.loads(response.read())
    provider = "%-30s %-20s %s\n" % ("Provider Name", " ", "UUID")
    for each in provider_data:
        provider = provider + "%-30s %-20s %s" % (each["name"], "-->", each["uuid"]) + "\n"
    print(provider)


@exception_handling
def get_regions_data(base_url, customer_uuid, auth_uuid):
    provider_url = base_url + "/api/v1/customers/" + customer_uuid + "/regions"
    response = call_api(provider_url, auth_uuid)
    region_data = json.loads(response.read())
    region = "%-30s %-20s %-20s %s\n\n" % ("Region Name", "Provider", "", "UUID")
    for each in region_data:
        zones = "\n\tZones:-\t\t name\t\t UUID"
        for each_zone in each["zones"]:
            zones = zones + "\n\t\t\t %s\t\t %s" % (each_zone["name"], each_zone["uuid"]) + "\n"
        region = region + "%-30s %-20s  %-20s %s"\
                 % (each["name"], each["provider"]["code"], each["uuid"], zones) + "\n"
    print(region)


@exception_handling
def get_universe_list(base_url, customer_uuid, auth_uuid):
    universe_url = base_url + "/api/v1/customers/" + customer_uuid + \
                          "/universes"
    response = call_api(universe_url, auth_uuid)
    universe_data = json.loads(response.read())
    universe = "%-30s %-20s %s\n" % ("Universe Name", " ", "UUID")
    for each in universe_data:
        universe = universe + "%-30s %-20s %s" % (each["name"], "-->", each["universeUUID"]) + "\n"
    print(universe)


def get_key_value(data, key):
    """
    Get the value of the response from the key to handle the response in bash.

    :param data: data.
    :param key: key.
    :return: None.
    """
    try:
        if data and data != "":
            json_data = json.loads(str(data))
            print(json_data.get(key))
        else:
            print("Action Failed")
    except Exception:
        print(data)


def handle_response(response_json):
    """
    Handle the response to bash according to the operations performed in the functions.

    :param response_json: data.
    :return: None
    """
    if response_json.get("status") == "success":
        print(response_json.get("data"))
    else:
        print(response_json.get("error"))
