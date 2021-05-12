# Copyright (c) YugaByte, Inc.
#
# Copyright 2021 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt


import json
import copy
import collections
import sys

python_version = sys.version_info[0]
if python_version == 2:
    from urllib2 import HTTPError
else:
    from urllib.error import HTTPError

SUCCESS = "success"
FAIL = "failed"

if __name__ == '__main__':
  raise RuntimeError('This file is not intended to run directly, please use yb_platform_util.sh instead')

def __generate_script_reponse(message, is_success=True, error_message=False):
    return {
        'data': message, 
        'status': SUCCESS if bool(is_success) else FAIL, 
        'error': error_message if error_message else message
        }

def exception_handling(func):
    def inner_function(*args, **kwargs):

        try:
            return func(*args, **kwargs)
        except HTTPError as e:
            content = e.read().decode('utf-8')
            if 'html>' in content:
                message = 'Invalid YB_PLATFORM_URL URL or params, Getting html page in response'
                sys.stderr.write(json.dumps(__generate_script_reponse(message, False)))
            else:
                sys.stderr.write(json.dumps(__generate_script_reponse(content, False)))
        except Exception as e:
            sys.stderr.write(json.dumps(__generate_script_reponse(str(e), False)))
    return inner_function


def __convert_unicode_json(data):
    """
    Function to convert unicode json to dictionary
    {u"name": u"universe"} => {"name": "universe"}
    
    :param data: Unicode json data.
    :return: Converted data
    """
    if python_version == 2:
        if isinstance(data, basestring):
            return str(data)
        elif isinstance(data, collections.Mapping):
            return dict(map(__convert_unicode_json, data.iteritems()))
        elif isinstance(data, collections.Iterable):
            return type(data)(map(__convert_unicode_json, data))
        else:
            return data
    else:
        return data


def __call_api(url, auth_uuid, data=None, is_delete=False):
    """
    Call the corresponding url with auth token, headers and returns the response.

    :param url: url to be called.
    :param auth_uuid: Authentication token of the customer.
    :param data: Sata for POST request.
    :param is_delete: To identify the delete call.
    :return: Response of the API call.
    """
    if python_version == 2:
        import urllib2
        request = urllib2.Request(url)
        if is_delete:
            request.get_method = lambda: 'DELETE'

        request.add_header('X-AUTH-YW-API-TOKEN', auth_uuid)
        request.add_header('Content-Type', 'application/json; charset=utf-8')
        if data:
            response = urllib2.urlopen(request, json.dumps(data).encode('utf-8'))
        else:
            response = urllib2.urlopen(request)
        return response
    else:
        import urllib.request
        if not is_delete:
            request = urllib.request.Request(url)
        else:
            request = urllib.request.Request(url, method='DELETE')

        request.add_header('X-AUTH-YW-API-TOKEN', auth_uuid)
        request.add_header('Content-Type', 'application/json; charset=utf-8')
        if data:
            response = urllib.request.urlopen(request, json.dumps(data).encode('utf-8'))
        else:
            response = urllib.request.urlopen(request)
        return response


def save_universe_details_to_file(base_url, customer_uuid, auth_uuid, universe_name, base_dir):
    """
    Get the universe details and store it in a json file after formatting the json.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :param universe_name: Name of the universe to take a json data from.
    :param base_dir: Base directory in which the json file should be stored.
    :return: None
    """
    universe = __get_universe_by_name(base_url, customer_uuid, auth_uuid, universe_name)
    if universe:
        configure_json = __create_universe_config(universe)
        file_path = f'{base_dir}/{universe_name}.json'
        with open(file_path, 'w') as file_obj:
            json.dump(configure_json, file_obj)
        
        response = __generate_script_reponse(f'Detail of universe have been saved to {str(file_path)}')
        __handle_response(response)
    else:
        response = __generate_script_reponse(f'Universe with {universe_name} is not found.', False)
        __handle_response(response)


def save_universe_details_to_file_by_uuid(base_url, customer_uuid, auth_uuid, universe_uuid, base_dir):
    """
    Get universe details from UUID and store it in json after formatting it.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :param universe_uuid: UUID of the universe to take a json data from.
    :param base_dir: Base directory in which the json file should be stored.
    :return: None
    """
    universe = __get_universe_by_uuid(base_url, customer_uuid, auth_uuid, universe_uuid)
    if universe:
        configure_json = __create_universe_config(universe)
        name = universe.get("name")
        file_path = f'{base_dir}/{name}.json'
        with open(file_path, 'w') as file_obj:
            json.dump(configure_json, file_obj)
        response = __generate_script_reponse(f'Detail of universe have been saved to {str(file_path)}')
        __handle_response(response)
    else:
        response = __generate_script_reponse('Universe details not found', False)
        __handle_response(response)

@exception_handling
def __create_universe_from_config(universe_config, base_url, customer_uuid, auth_uuid):
    """
    Create the universe from universe config data by calling universe POST API.

    :param universe_config: Universe config data.
    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :return: None
    """
    universe_create_url = f'{base_url}/api/v1/customers/{customer_uuid}/universes'
    response = __call_api(universe_create_url, auth_uuid, universe_config)
    universe_json = __convert_unicode_json(json.loads(response.read()))
    task_id = universe_json['taskUUID']
    print(__generate_script_reponse(task_id))


def create_universe(base_url, customer_uuid, auth_uuid, input_file, universe_name=''):
    """
    Create the universe using the json and provided universe name.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :param input_file: Directory of the stored universe json data.
    :param universe_name: Name of the new universe to be created.
    :return: None
    """
    configure_json = __modify_universe_config(input_file, universe_name)
    universe_config_json = __post_universe_config(configure_json, base_url, customer_uuid, auth_uuid)
    # add the missing fields in the config json.
    universe_config_json['clusterOperation'] = 'CREATE'
    universe_config_json['currentClusterType'] = 'PRIMARY'
    # Call universe config api to get the update config data.
    universe_config_json = __post_universe_config(
        universe_config_json,
        base_url,
        customer_uuid,
        auth_uuid
    )
    if universe_config_json:
        __create_universe_from_config(universe_config_json, base_url, customer_uuid, auth_uuid)
    else:
        data = __generate_script_reponse(f'Unable to Create Universe for Customer {customer_uuid}', False)
        __handle_response(data)

@exception_handling
def get_task_details(task_id, base_url, customer_uuid, auth_uuid):
    """
    Get details of the ongoing task.

    :param task_id: Task UUID.
    :param base_url: Base url of the platform back end.
    :param customer_uuid: Authentication token for the customer.
    :param auth_uuid: Authentication token for the customer.
    :return: None
    """
    task_url = f'{base_url}/api/v1/customers/{customer_uuid}/tasks/{task_id}'
    response = __call_api(task_url, auth_uuid)
    task_json = __convert_unicode_json(json.loads(response.read()))
    if task_json.get('status') == 'Running':
        print(__generate_script_reponse(int(task_json.get('percent'))))
    elif task_json.get('status') == 'Success':
        print(__generate_script_reponse(100))
    elif task_json.get('status') == 'Failure':
        content = {
            'message': f'{task_json.get("title")} failed',
            'details': task_json.get('details')
        }
        print(__generate_script_reponse(content))


def __create_universe_config(universe_data):
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
        'uuid',
        'storageType',
        'awsArnString',
        'useHostname',
        'preferredRegion',
        'regions',
        'index',
        'placementInfo'
    ]

    clusters_list = __get_cluster_list(clusters, excluded_keys)
    configure_json['clusters'] = copy.deepcopy(clusters_list)
    configure_json['clusterOperation'] = 'CREATE'
    configure_json['userAZSelected'] = copy.deepcopy(user_az_selected)
    configure_json['currentClusterType'] = 'PRIMARY'

    return configure_json


def __get_cluster_list(clusters, excluded_keys):
    """
    Method to modify payload for create API. Modify user Intent inside cluster list.

    :param clusters: List of clusters.
    :param excluded_keys: Keys to be excluded
    :param universe_name: Name of the new universe to be created.
    :return: Cluster list.
    """
    clusters_list = []
    for each_cluster in clusters:
        cluster = {}
        for key, val in each_cluster.items():
            if key not in excluded_keys:
                user_intent = {}
                if key == 'userIntent':
                    for key1, val1 in val.items():
                        if key1 not in excluded_keys:
                            # Remove unnecessary deviceinfo from create API payload.
                            if key1 == 'deviceInfo':
                                val1.pop('diskIops')
                                val1.pop('storageType')
                            user_intent[key1] = val1
                    cluster[key] = user_intent
                else:
                    cluster[key] = val
        clusters_list.append(cluster)
    return clusters_list


def __modify_universe_config(file_name, universe_name=''):
    """
    Modify the universe json with new name.

    :param file_name: Name of the json file.
    :param universe_name: New universe name.
    :return: Modified universe config data.
    """
    data = {}
    with open(file_name) as f:
        data = __convert_unicode_json(json.loads(f.read()))

    clusters = data.get('clusters')
    if universe_name:
        for each_cluster in clusters:
            each_cluster['userIntent']['universeName'] = universe_name
    return data


@exception_handling
def __post_universe_config(configure_json, base_url, customer_uuid, auth_uuid):
    """
    Call the universe config URL with the updated data.

    :param configure_json: Universe config json.
    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :return: None
    """
    universe_config_url = f'{base_url}/api/v1/customers/{customer_uuid}/universe_configure'
    response = __call_api(universe_config_url, auth_uuid, configure_json)
    universe_config_json = __convert_unicode_json(json.loads(response.read()))
    return universe_config_json


@exception_handling
def __get_universe_by_name(base_url, customer_uuid, auth_uuid, universe_name):
    """
    Get universe data by name of the universe.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :param universe_name: Universe name.
    :return: None
    """
    universe_url = f'{base_url}/api/v1/customers/{customer_uuid}/universes'
    response = __call_api(universe_url, auth_uuid)
    data = __convert_unicode_json(json.load(response))
    for universe in data:
        if universe.get('name') == universe_name:
            del universe['pricePerHour']
            return universe
    return None

@exception_handling
def __get_universe_by_uuid(base_url, customer_uuid, auth_uuid, universe_uuid):
    """
    Get universe details by UUID of the universe.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :param universe_uuid: UUID of the universe.
    :return: None
    """
    universe_config_url = f'{base_url}/api/v1/customers/{customer_uuid}/universes/{universe_uuid}'
    response = __call_api(universe_config_url, auth_uuid)
    return __convert_unicode_json(json.load(response))


def get_universe_uuid_by_name(base_url, customer_uuid, auth_uuid, universe_name):
    """
    Get the UUID of the universe.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid:  Authentication token for the customer.
    :param universe_name: Name of the universe.
    :return: None.
    """
    universe = __get_universe_by_name(base_url, customer_uuid, auth_uuid, universe_name)
    if universe and 'universeUUID' in universe:
        print(__generate_script_reponse(universe.get('universeUUID')))
    else:
        message = f'Universe with {universe_name} not found'
        sys.stderr.write(json.dumps(__generate_script_reponse(message, False)))


@exception_handling
def get_single_customer_uuid(base_url, auth_uuid):
    """
    Get customer UUID. Raise error in case multiple customer is present. 

    :param base_url: Base url of the platform back end.
    :param auth_uuid: Authentication token for the customer.
    :return: None
    """
    customer_url = f'{base_url}/api/v1/customers'
    response = __call_api(customer_url, auth_uuid)
    data = __convert_unicode_json(json.load(response))
    if (len(data) == 1):
        print(__generate_script_reponse(str(data[0])))
    else:
        response = __generate_script_reponse(data, False, 'Please provide customer UUID')
        sys.stderr.write(response)


@exception_handling
def delete_universe_by_id(base_url, customer_uuid, auth_uuid, universe_uuid, force_delete=None):
    """
    Delete the universe by providing UUID.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :param universe_uuid: UUID of the universe to be deleted.
    :return:
    """
    universe_delete_url = f'{base_url}/api/v1/customers/{customer_uuid}/universes/{universe_uuid}'

    if force_delete:
        universe_delete_url += '?isForceDelete=true'
    response = __call_api(universe_delete_url, auth_uuid, is_delete=True)
    universe_json = __convert_unicode_json(json.loads(response.read()))
    task_id = universe_json['taskUUID']
    print(__generate_script_reponse(task_id))


@exception_handling
def get_provider_data(base_url, customer_uuid, auth_uuid):
    """
    Delete the universe by providing UUID.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :return: All available providers
    """
    provider_url = f'{base_url}/api/v1/customers/{customer_uuid}/providers'
    response = __call_api(provider_url, auth_uuid)
    provider_data = __convert_unicode_json(json.loads(response.read()))
    providers = []
    for each in provider_data:
        provider = {}
        provider['name'] = each.get('name')
        provider['uuid'] = each.get('uuid')
        providers.append(provider)
    print(json.dumps(providers))


@exception_handling
def get_regions_data(base_url, customer_uuid, auth_uuid):
    """
    Delete the universe by providing UUID.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :return: All available regions
    """
    provider_url = f'{base_url}/api/v1/customers/{customer_uuid}/regions'
    response = __call_api(provider_url, auth_uuid)
    region_data = __convert_unicode_json(json.loads(response.read()))
    regions = []
    for each in region_data:
        zones = []
        for each_zone in each.get('zones'):
            zone = {}
            zone['name'] = each_zone.get('name')
            zone['uuid'] = each_zone.get('uuid')
            zones.append(zone)
        region = {}
        region['zones'] = zones
        region['name'] = each.get('name')
        region['uuid'] = each.get('uuid')
        region['provider'] = each.get('provider').get('code')
        regions.append(region)
    print(json.dumps(regions))


@exception_handling
def get_universe_list(base_url, customer_uuid, auth_uuid):
    """
    Delete the universe by providing UUID.

    :param base_url: Base url of the platform back end.
    :param customer_uuid: UUID of the customer.
    :param auth_uuid: Authentication token for the customer.
    :return: List of universe name and UUID
    """
    universe_url = f'{base_url}/api/v1/customers/{customer_uuid}/universes'
    response = __call_api(universe_url, auth_uuid)
    universe_data = __convert_unicode_json(json.loads(response.read()))
    universes = []
    for each in universe_data:
        universe = {}
        universe['name'] = each.get('name')
        universe['universeUUID'] = each.get('universeUUID')
        universes.append(universe)
    print(json.dumps(universes))


def get_key_value(data, key):
    """
    Get the value of the response from the key to handle the response in bash.

    :param data: Data.
    :param key: Key.
    :return: None.
    """
    try:
        if data and data != '':
            json_data = __convert_unicode_json(json.loads(str(data)))
            print(json_data.get(key))
        else:
            print('Action Failed')
    except Exception:
        print(data)


def __handle_response(response_json):
    """
    Handle the response to bash according to the operations performed in the functions.

    :param response_json: Data.
    :return: None
    """
    if response_json.get('status') == 'success':
        print(response_json.get('data'))
    else:
        sys.stderr.write(response_json.get('error'))
