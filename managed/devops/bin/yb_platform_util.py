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
import os
import copy
import collections
import sys
import argparse
import textwrap
import time
from argparse import RawDescriptionHelpFormatter

python_version = sys.version_info[0]
if python_version == 2:
    from urllib2 import HTTPError
else:
    from urllib.error import HTTPError

SUCCESS = "success"
FAIL = "failed"
script_path = os.path.dirname(os.path.abspath( __file__ ))


def exception_handling(func):
    def inner_function(*args, **kwargs):

        try:
            return func(*args, **kwargs)
        except HTTPError as e:
            content = e.read().decode('utf-8')
            if 'html>' in content:
                message = 'Invalid YB_PLATFORM_URL URL or params, Getting html page in response'
                sys.stderr.write(message)
            else:
                sys.stderr.write(content)
        except Exception as e:
            sys.stderr.write(str(e))
    return inner_function


class YBUniverse(YBUniverseAction):
     def __init__(self):
        self.parse_arguments()
        self.base_url = os.environ.get('YB_PLATFORM_URL', '')
        self.customer_uuid = ''
        self.api_token = os.environ.get('YB_PLATFORM_API_TOKEN', '')

    
    def __call_api(self, url, data=None, is_delete=False):
        """
        Call the corresponding url with auth token, headers and returns the response.

        :param url: url to be called.
        :param data: data for POST request.
        :param is_delete: To identify the delete call.
        :return: Response of the API call.
        """
        if python_version == 2:
            import urllib2
            request = urllib2.Request(url)
            if is_delete:
                request.get_method = lambda: 'DELETE'

            request.add_header('X-AUTH-YW-API-TOKEN', self.api_token)
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

            request.add_header('X-AUTH-YW-API-TOKEN', self.api_token)
            request.add_header('Content-Type', 'application/json; charset=utf-8')
            if data:
                response = urllib.request.urlopen(request, json.dumps(data).encode('utf-8'))
            else:
                response = urllib.request.urlopen(request)
            return response
    

    def __convert_unicode_json(self, data):
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
                return dict(map(self.__convert_unicode_json, data.iteritems()))
            elif isinstance(data, collections.Iterable):
                return type(data)(map(self.__convert_unicode_json, data))
            else:
                return data
        else:
            return data
    

    def __get_universe_by_name(self, universe_name):
        """
        Get universe data by name of the universe.

        :param universe_name: Universe name.
        :return: None
        """
        universe_url = '{0}/api/v1/customers/{1}/universes'.format(self.base_url, self.customer_uuid)
        response = self.__call_api(universe_url)
        data = self.__convert_unicode_json(json.load(response))
        for universe in data:
            if universe.get('name') == universe_name:
                del universe['pricePerHour']
                return universe
        return None
    
    
    def __create_universe_config(self, universe_data):
        """
        Create the universe config data from the json file.

        :param universe_data: Stored universe data.
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

        clusters_list = self.__get_cluster_list(clusters, excluded_keys)
        configure_json['clusters'] = copy.deepcopy(clusters_list)
        configure_json['clusterOperation'] = 'CREATE'
        configure_json['userAZSelected'] = copy.deepcopy(user_az_selected)
        configure_json['currentClusterType'] = 'PRIMARY'

        return configure_json
    

    @staticmethod
    def __get_cluster_list(clusters, excluded_keys):
        """
        Method to modify payload for create API. Modify user Intent inside cluster list.

        :param clusters: List of clusters.
        :param excluded_keys: Keys to be excluded
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


    def __get_universe_by_uuid(self, universe_uuid):
        """
        Get universe details by UUID of the universe.

        :param universe_uuid: UUID of the universe.
        :return: None
        """
        universe_config_url = '{0}/api/v1/customers/{1}/universes/{2}'.format(
            self.base_url, self.customer_uuid, universe_uuid)
        response = self.__call_api(universe_config_url)
        return self.__convert_unicode_json(json.load(response))
    

    def __create_universe_from_config(self, universe_config):
        """
        Create the universe from universe config data by calling universe POST API.

        :param universe_config: Universe config data.
        :return: None
        """
        universe_create_url = '{0}/api/v1/customers/{1}/universes'.format(self.base_url, self.customer_uuid)
        response = self.__call_api(universe_create_url, universe_config)
        universe_json = self.__convert_unicode_json(json.loads(response.read()))
        return universe_json['taskUUID']


    def __modify_universe_config(self, file_name, universe_name=''):
        """
        Modify the universe json with new name.

        :param file_name: Name of the json file.
        :param universe_name: New universe name.
        :return: Modified universe config data.
        """
        data = {}
        with open(file_name) as f:
            data = self.__convert_unicode_json(json.loads(f.read()))

        clusters = data.get('clusters')
        if universe_name:
            for each_cluster in clusters:
                each_cluster['userIntent']['universeName'] = universe_name
        return data


    def __post_universe_config(self, configure_json):
        """
        Call the universe config URL with the updated data.

        :param configure_json: Universe config json.
        :return: None
        """
        universe_config_url = '{0}/api/v1/customers/{1}/universe_configure'.format(
            self.base_url, self.customer_uuid)
        response = self.__call_api(universe_config_url, configure_json)
        universe_config_json = self.__convert_unicode_json(json.loads(response.read()))
        return universe_config_json


    def __get_universe_list(self):
        """
        Delete the universe by providing UUID.

        :return: List of universe name and UUID
        """
        universe_url = '{0}/api/v1/customers/{1}/universes'.format(self.base_url, self.customer_uuid)
        response = self.__call_api(universe_url)
        universe_data = self.__convert_unicode_json(json.loads(response.read()))
        universes = []
        for each in universe_data:
            universe = {}
            universe['name'] = each.get('name')
            universe['universeUUID'] = each.get('universeUUID')
            universes.append(universe)
        print(json.dumps(universes))
    

    def __save_universe_details_to_file(self, universe_name):
        """
        Get the universe details and store it in a json file after formatting the json.

        :param universe_name: Name of the universe to take a json data from.
        :return: None
        """
        universe = self.__get_universe_by_name(universe_name)
        if universe:
            configure_json = self.__create_universe_config(universe)
            file_path = '{0}/{1}.json'.format(script_path, universe_name)
            with open(file_path, 'w') as file_obj:
                json.dump(configure_json, file_obj)
            
            print('Detail of universe have been saved to {0}'.format(str(file_path)))
        else:
            print('Universe with {0} is not found.'.format(universe_name))


    def __save_universe_details_to_file_by_uuid(self, universe_uuid):
        """
        Get universe details from UUID and store it in json after formatting it.

        :param universe_uuid: UUID of the universe to take a json data from.
        :return: None
        """
        universe = self.__get_universe_by_uuid(universe_uuid)
        if universe:
            configure_json = self.__create_universe_config(universe)
            name = universe.get("name")
            file_path = '{0}/{1}.json'.format(script_path, name)
            with open(file_path, 'w') as file_obj:
                json.dump(configure_json, file_obj)
            print('Detail of universe have been saved to {0}'.format(str(file_path)))
        else:
            print('Universe with {0} is not found.'.format(universe_uuid))
        
    
    def __task_progress_bar(self, task_id):
        """
        Show the task bar to the user according to task completion.

        :return: None
        """
        toolbar_width = 100
        # each hash represents 2 % of the progress
        while True:

            progress = self.__get_task_details(task_id)
            sys.stdout.write("[%s]"%(("-")*toolbar_width))
            sys.stdout.write("{0}%".format(progress))
            sys.stdout.flush()
            sys.stdout.write("\r") # return to start of line
            sys.stdout.flush()
            sys.stdout.write("[")#Overwrite over the existing text from the start 
            sys.stdout.write("#"*(progress))# number of # denotes the progress completed
            sys.stdout.write('\n')
            sys.stdout.flush()
            if progress == progress:
                exit()
            time.sleep(20)


    def __get_task_details(self, task_id):
        """
        Get details of the ongoing task.

        :param task_id: Task UUID.
        :return: None
        """
        task_url = '{0}/api/v1/customers/{1}/tasks/{2}'.format(self.base_url, self.customer_uuid, task_id)
        response = self.__call_api(task_url)
        task_json = self.__convert_unicode_json(json.loads(response.read()))
        if task_json.get('status') == 'Running':
            return int(task_json.get('percent'))
        elif task_json.get('status') == 'Success':
            return 100
        elif task_json.get('status') == 'Failure':
            content = {
                'message': '{0} failed'.format(task_json.get("title")),
                'details': task_json.get('details')
            }
            sys.stderr.write(json.dumps(content))
            exit()


    def __get_universe_uuid_by_name(self, universe_name):
        """
        Get the UUID of the universe.

        :param universe_name: Name of the universe.
        :return: None.
        """
        universe = self.__get_universe_by_name(universe_name)
        if universe and 'universeUUID' in universe:
            return universe.get('universeUUID')
        else:
            message = 'Universe with {0} not found'.format(universe_name)
            sys.stderr.write(message)
            exit()


    def __delete_universe_by_id(self, universe_uuid):
        """
        Delete the universe by providing UUID.

        :param universe_uuid: UUID of the universe to be deleted.
        :return:
        """
        universe_delete_url = '{0}/api/v1/customers/{1}/universes/{2}'.format(
            self.base_url, self.customer_uuid, universe_uuid)

        if self.args.force:
            universe_delete_url += '?isForceDelete=true'
        response = self.__call_api(universe_delete_url, is_delete=True)
        universe_json = self.__convert_unicode_json(json.loads(response.read()))
        task_id = universe_json['taskUUID']

        print('Universe delete requested successfully.')
        print('Use {0} as task id to get status of universe.'.format(task_id))
        if not self.args.no_wait:
            self.__task_progress_bar(task_id)


    def create_universe(self):
        """
        Create the universe using the json and provided universe name.

        :return: None
        """
        if not self.args.file:
            print("Input json file required. Use \`-f|--file <file_path>\` to pass json input file.")
            exit()
        input_file = script_path + '/' + self.args.file
        universe_name = self.args.universe_name
        configure_json = self.__modify_universe_config(input_file, universe_name)
        universe_config_json = self.__post_universe_config(configure_json)
        # add the missing fields in the config json.
        universe_config_json['clusterOperation'] = 'CREATE'
        universe_config_json['currentClusterType'] = 'PRIMARY'
        # Call universe config api to get the update config data.
        universe_config_json = self.__post_universe_config(universe_config_json)
        if universe_config_json:
            task_id = self.__create_universe_from_config(universe_config_json)
            print('Universe create requested successfully.')
            print('Use {0} as task id to get status of universe.'.format(task_id))
            if not self.args.no_wait:
                self.__task_progress_bar(task_id)
        else:
            print('Unable to Create Universe for Customer {0}'.format(customer_uuid))


    def get_single_customer_uuid(self):
        """
        Get customer UUID. Raise error in case multiple customer is present. 

        :return: None
        """
        customer_url = '{0}/api/v1/customers'.format(self.base_url)
        response = self.__call_api(customer_url)
        data = self.__convert_unicode_json(json.load(response))
        if (len(data) == 1):
            self.customer_uuid = str(data[0])
        else:
            sys.stderr.write('Muliple customer UUID present, Please provide customer UUID')
            exit()


    def get_regions_data(self):
        """
        Delete the universe by providing UUID.

        :return: All available regions
        """
        provider_url = '{0}/api/v1/customers/{1}/regions'.format(self.base_url, self.customer_uuid)
        response = self.__call_api(provider_url)
        region_data = self.__convert_unicode_json(json.loads(response.read()))
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
    

    def get_provider_data(self):
        """
        Delete the universe by providing UUID.

        :return: All available providers
        """
        provider_url = '{0}/api/v1/customers/{1}/providers'.format(self.base_url, self.customer_uuid)
        response = self.__call_api(provider_url)
        provider_data = self.__convert_unicode_json(json.loads(response.read()))
        providers = []
        for each in provider_data:
            provider = {}
            provider['name'] = each.get('name')
            provider['uuid'] = each.get('uuid')
            providers.append(provider)
        print(json.dumps(providers))


    def delete_universe(self):
        """
        Function to do delete action.

        :return: None
        """
        confirmation = self.args.yes and 'y'
        if not confirmation:
            confirmation = input("Continue with deleting universe(y/n)? :")
        if confirmation.lower()[0] == 'y':
            if not self.args.force:
                print('Note:- Universe deletion can fail due to errors, Use `--force` to ignore errors and force delete.')
            name = self.args.universe_name
            universe_uuid = self.args.universe_uuid
            if not (name or uuid):
                print("Required universe name | uuid to delete universe.")
                print("Use \`-n|--universe_name <universe_name>\` to pass universe name.")
                print("Use \`-u|--universe_uuid <universe_uuid>\` to pass universe uuid.")
                exit()
        
            if self.args.universe_name:
                universe_uuid = self.__get_universe_uuid_by_name(self.args.universe_name)
            self.__delete_universe_by_id(universe_uuid)
        else:
            print("Aborted Delete universe")
            exit()


    def get_universe(self):
        """
        Function to get universe list. Function to get universe detail from UUID/name.

        :return: None
        """
        if self.args.universe_name:
            self.__save_universe_details_to_file(self.args.universe_name)
        elif self.args.universe_uuid:
            self.__save_universe_details_to_file_by_uuid(self.args.universe_uuid)
        else:
            self.__get_universe_list()
    

    def task_status(self):
        """
        Function to update task status.

        :return: None
        """
        self.__task_progress_bar(self.args.task)
    

    def parse_arguments(self):
        """
        Function to dispaly help message, Add arguments to the python script.

        :return: None
        """
        parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=textwrap.dedent('''
        Script to perform universe actions.
        --------------------------------
            1. Get existing universe list. 
            2. Get existing universe details by universe name in json format.
            3. Get existing universe details by universe UUID in json format.
            4. Delete existing universe by universe name.
            5. Delete existing universe by universe UUID.
            6. Create a new universe from a json config file.
            7. Get task progress. 
            8. Get list of available regions with availability zones. 
            9. Get list of available providers. 

        Required to export variable:
        --------------------------------
            YB_PLATFORM_URL 
                API URL for yugabyte
                Example: 
                    export YB_PLATFORM_URL=http://localhost:9000
            YB_PLATFORM_API_TOKEN 
                API token for Yugabyte API
                Example: 
                    export YB_PLATFORM_API_TOKEN=e16d75cf-79af-4c57-8659-2f8c34223551

        Actions:
        --------------------------------
        get_universe 
            Get the details of an existing universe as a json file
            Example: 
                python yb_platform_util.py get_universe

            Universe json with universe name: 
                python yb_platform_util.py get_universe -n test-universe

        add_universe |  create_universe
            Create universe from json file
            Example: 
                python yb_platform_util.py create_universe -f test-universe.json -n test-universe-by-name

        del_universe |  delete_universe
            Delete an existing universe
            Example: 
                python yb_platform_util.py delete_universe -n test-universe

        task_status 
            To get task status
            Example: 
                python yb_platform_util.py task_status -t f33e3c9b-75ab-4c30-80ad-cba85646ea39

        get_provider 
            To get list of available providers
            Example: 
                python yb_platform_util.py get_provider

        get_region | get_az 
            List of available region with availability zones
            Example: 
                python yb_platform_util.py get_region
        ''')
        )
        parser.add_argument('actions', type=str, nargs=1, help='Universe actions to perform')
        parser.add_argument('-c', '--customer_uuid', help='Mandatory if multiple customer uuids present.')
        parser.add_argument('-n', '--universe_name', help='Universe name')
        parser.add_argument('-u', '--universe_uuid', help='Universe UUID')
        parser.add_argument('-f', '--file', help='Json input file for creating universe', metavar='INPUT_FILE_PATH')
        parser.add_argument('-t', '--task', help='Task UUID to get task status', metavar='TASK_ID')
        parser.add_argument('-y', '--yes', action='store_true', help='Input yes for all confirmation prompts')
        parser.add_argument('--no_wait', action='store_true', 
            help='To run command in background and do not wait for task completion task')
        parser.add_argument('--force', action='store_true', help='Force delete universe')
        self.args = parser.parse_args()
        self.parser = parser
    

    def invalid_action(self):
        """
        Hendle invalid action passed.

        :return: None
        """
        print("Invalid action\n")
        self.parser.print_help()


    def validate_env_values(self):
        """
        Validate if both env variable has been set.

        :return: None
        """
        if not self.base_url:
            print("YB_PLATFORM_URL is not set. Set YB_PLATFORM_URL as env variable to proceed.")
            print("\texport YB_PLATFORM_URL=<base_url_of_platform>\n\nExample:")
            print("\texport YB_PLATFORM_URL=http://localhost:9000\n")
        if not self.api_token:
            print("YB_PLATFORM_API_TOKEN is not set. Set YB_PLATFORM_API_TOKEN as env variable to proceed.")
            print("\texport YB_PLATFORM_API_TOKEN=<platform_api_token>\n\nExample:")
            print("\t export YB_PLATFORM_API_TOKEN=e16d75cf-79af-4c57-8659-2f8c34223551\n")
        if not (self.base_url or self.api_token):
            exit()


    @exception_handling
    def run(self):
        """
        Call function according to action passed by users.

        :return: None
        """
        self.validate_env_values()
        self.get_single_customer_uuid()
        action_dict = {
            'get_provider': self.get_provider_data,
            'get_region': self.get_regions_data,
            'get_az': self.get_regions_data,
            'get_universe': self.get_universe,
            'create_universe': self.create_universe,
            'add_universe': self.create_universe,
            'delete_universe': self.delete_universe,
            'del_universe': self.delete_universe,
            'task_status': self.task_status
            }

        action_dict.get(self.args.actions[0].lower(), self.invalid_action)()


if __name__ == '__main__':
    YBUniverse().run()
