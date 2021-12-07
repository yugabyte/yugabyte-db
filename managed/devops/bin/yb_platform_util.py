# #!/usr/bin/env python
# Copyright (c) YugaByte, Inc.
#
# Copyright 2021 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

"""
Python script to perfrom Universe operations.
"""
import enum
import json
import os
import copy
import collections
import sys
import argparse
import textwrap
import time

PYTHON_VERSION = sys.version_info[0]
if PYTHON_VERSION == 2:
    from urllib2 import HTTPError, urlopen, Request
else:
    from urllib.request import urlopen, Request
    from urllib.error import HTTPError

SUCCESS = "success"
FAIL = "failed"
PROGRESS_BAR = 100
SCRIPT_PATH = os.getcwd()


class HelpMessage(enum.Enum):
    """
    Enum for Help messages.
    """
    ACTION = 'Universe actions to perform'
    CUSTOMER_UUID = 'Mandatory if multiple customer uuids present.'
    UNIVERSE_NAME = 'Universe name'
    UNIVERSE_UUID = 'Universe UUID'
    FILE = 'Json input file for creating universe. Relative path.'
    TASK = 'Task UUID to get task status'
    YES = 'Input yes for all confirmation prompts'
    INTERVAL = 'Set interval time to get status update'
    NO_WAIT = 'To run command in background and do not wait for task completion task'
    FORCE = 'Force delete universe'


def exception_handling(func):
    """
    General exception handling for all actions
    """
    def inner_function(*args, **kwargs):
        """
        Wrape fucn with try and catch
        """
        try:
            return func(*args, **kwargs)
        except HTTPError as exception:
            try:
                content = exception.read().decode('utf-8')
                json.loads(content)
                sys.stderr.write(content)
            except ValueError as exception:
                message = 'Invalid YB_PLATFORM_URL URL, params or env values.\n'
                sys.stderr.write(message)
                sys.stderr.write(str(exception))
        except ValueError as exception:
            sys.stderr.write(str(exception))
        except Exception as exception:
            sys.stderr.write(str(exception))
        return None
    return inner_function


def convert_unicode_json(data):
    """
    Function to convert unicode json to dictionary
    {u"name": u"universe"} => {"name": "universe"}

    :param data: Unicode json data.
    :return: Converted data
    """
    if PYTHON_VERSION == 2:
        if isinstance(data, basestring):
            return str(data)
        if isinstance(data, collections.Mapping):
            return dict(map(convert_unicode_json, data.iteritems()))
        if isinstance(data, collections.Iterable):
            return type(data)(map(convert_unicode_json, data))
    return data


def check_positive(value):
    """
    Function to validate positive integer.

    :param data: value.
    :return: positive int
    """
    ivalue = int(value)
    if ivalue <= 0:
        raise argparse.ArgumentTypeError("%s is an invalid positive int value" % value)
    return ivalue


def get_input_from_user(message):
    """
    Function to get input from the user
    """
    return raw_input(message) if PYTHON_VERSION == 2 else input(message)


class YBUniverse():
    """
    Class to perform all UI opperation
    """
    def __init__(self):
        """
        Initialized initial values of class.
        """
        self.__parse_arguments()
        self.base_url = os.getenv('YB_PLATFORM_URL')
        self.customer_uuid = None
        self.api_token = os.getenv('YB_PLATFORM_API_TOKEN')

    def __call_api(self, url, data=None, is_delete=False):
        """
        Call the corresponding url with auth token, headers and returns the response.

        :param url: url to be called.
        :param data: data for POST request.
        :param is_delete: To identify the delete call.
        :return: Response of the API call.
        """
        if PYTHON_VERSION == 2:
            request = Request(url)
            if is_delete:
                request.get_method = lambda: 'DELETE'
        else:
            if is_delete:
                request = Request(url, method='DELETE')
            else:
                request = Request(url)

        request.add_header('X-AUTH-YW-API-TOKEN', self.api_token)
        request.add_header('Content-Type', 'application/json; charset=utf-8')
        if data:
            converted_data = json.dumps(data).encode('utf-8')
            response = urlopen(request, converted_data)
        else:
            response = urlopen(request)
        return convert_unicode_json(json.load(response))

    def __get_universe_by_name(self, universe_name):
        """
        Get universe data by name of the universe.

        :param universe_name: Universe name.
        :return: None or universe object
        """
        universe_url = '{0}/api/v1/customers/{1}/universes'.format(
            self.base_url, self.customer_uuid)
        data = self.__call_api(universe_url)
        for universe in data:
            if universe.get('name') == universe_name:
                del universe['pricePerHour']
                return universe
        return None

    def __create_universe_config(self, universe_data):
        """
        Create the universe config data from the json file.
        Remove extra fields from universe details before saving to the file.
        User will use this file to create new universe,
        We need to remove all feild which are not required while create.

        :param universe_data: Stored universe data.
        :return: Configured universe json.
        """
        configure_json = {}
        clusters = copy.deepcopy(universe_data['universeDetails']['clusters'])
        user_az_selected = universe_data['universeDetails']['userAZSelected']

        # All excluded_keys will be populated by universe_config api.
        excluded_keys = [
            'uuid',
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
        Remove extra fields from clusters_list before saving to the file.
        User will use this config to create new universe,
        We need to remove all feild which are not required while create.

        :param clusters: List of clusters.
        :param excluded_keys: Keys to be excluded
        :return: Cluster list.
        """
        clusters_list = []
        for each_cluster in clusters:
            user_intent = each_cluster.get('userIntent', {})
            for key in excluded_keys:
                each_cluster.pop(key, None)
                user_intent.pop(key, None)
            # diskIops is extra field for create payload.
            user_intent.get('deviceInfo', {}).pop('diskIops', None)
            clusters_list.append(each_cluster)
        return clusters_list

    def __get_universe_by_uuid(self, universe_uuid):
        """
        Get universe details by UUID of the universe.

        :param universe_uuid: UUID of the universe.
        :return: None
        """
        universe_config_url = '{0}/api/v1/customers/{1}/universes/{2}'.format(
            self.base_url, self.customer_uuid, universe_uuid)
        return self.__call_api(universe_config_url)

    def __create_universe_from_config(self, universe_config):
        """
        Create the universe from universe config data by calling universe POST API.

        :param universe_config: Universe config data.
        :return: None
        """
        universe_create_url = '{0}/api/v1/customers/{1}/universes'.format(
            self.base_url, self.customer_uuid)
        universe_json = self.__call_api(universe_create_url, universe_config)
        return universe_json['taskUUID']

    @staticmethod
    def __modify_universe_config(data, universe_name=''):
        """
        Modify the universe json with new name.

        :param file_name: Name of the json file.
        :param universe_name: New universe name.
        :return: Modified universe config data.
        """

        clusters = data.get('clusters')
        for each_cluster in clusters:
            if universe_name:
                each_cluster['userIntent']['universeName'] = universe_name
            else:
                universe_name = each_cluster['userIntent']['universeName']
                break
        return data, universe_name

    def __post_universe_config(self, configure_json):
        """
        Call the universe config URL with the updated data.

        :param configure_json: Universe config json.
        :return: None
        """
        universe_config_url = '{0}/api/v1/customers/{1}/universe_configure'.format(
            self.base_url, self.customer_uuid)
        return self.__call_api(universe_config_url, configure_json)

    def __get_universe_list(self):
        """
        Print list of universe names and UUID's.

        :return: None
        """
        universe_url = '{0}/api/v1/customers/{1}/universes'.format(
            self.base_url, self.customer_uuid)
        universe_data = self.__call_api(universe_url)
        universes = []
        for each_universe in universe_data:
            universes.append({
                'name': each_universe.get('name'),
                'universeUUID': each_universe.get('universeUUID')
            })
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
            file_name = '{0}.json'.format(universe_name)
            file_path = os.path.join(SCRIPT_PATH, file_name)
            with open(file_path, 'w') as file_obj:
                json.dump(configure_json, file_obj)

            print('Detail of universe have been saved to {0}'.format(str(file_path)))
        else:
            sys.stderr.write('Universe with {0} is not found.'.format(universe_name))

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
            file_name = '{0}.json'.format(name)
            file_path = os.path.join(SCRIPT_PATH, file_name)
            with open(file_path, 'w') as file_obj:
                json.dump(configure_json, file_obj)
            print('Detail of universe have been saved to {0}'.format(str(file_path)))
        else:
            sys.stderr.write('Universe with {0} is not found.'.format(universe_uuid))

    def __task_progress_bar(self, task_id):
        """
        Show the task bar to the user according to task completion.

        :return: None
        """
        while True:
            progress = self.__get_task_details(task_id)
            sys.stdout.write("[%s]" % (("-") * PROGRESS_BAR))
            sys.stdout.write("{0}%".format(progress))
            # return to start of line
            sys.stdout.write("\r")
            sys.stdout.flush()
            # Overwrite over the existing text from the start
            sys.stdout.write("[")
            # number of # denotes the progress completed
            sys.stdout.write("#" * (progress))
            sys.stdout.write('\n')
            sys.stdout.flush()
            if progress >= 100:
                sys.exit()
            time.sleep(self.args.interval)

    def __get_task_details(self, task_id):
        """
        Get Progress of task.

        :param task_id: Task UUID.
        :return: None
        """
        task_url = '{0}/api/v1/customers/{1}/tasks/{2}'.format(
            self.base_url, self.customer_uuid, task_id)
        task_json = self.__call_api(task_url)
        status = task_json.get('status')
        if status == 'Running':
            percentage = int(task_json.get('percent'))
            # If status is running and percentage 100 means task is not started yet.
            return 0 if percentage == 100 else int(task_json.get('percent'))
        if status == 'Success':
            return 100
        if status == 'Failure':
            content = {
                'message': '{0} failed'.format(task_json.get("title")),
                'details': task_json.get('details')
            }
            raise ValueError(content)
        return 0

    def __get_universe_uuid_by_name(self, universe_name):
        """
        Get the UUID of the universe.

        :param universe_name: Name of the universe.
        :return: None.
        """
        universe = self.__get_universe_by_name(universe_name)
        if universe and 'universeUUID' in universe:
            return universe.get('universeUUID')
        message = 'Universe with {0} not found'.format(universe_name)
        raise ValueError(message)

    def __delete_universe_by_uuid(self, universe_uuid):
        """
        Delete the universe by providing UUID.

        :param universe_uuid: UUID of the universe to be deleted.
        :return:
        """
        universe_delete_url = '{0}/api/v1/customers/{1}/universes/{2}'.format(
            self.base_url, self.customer_uuid, universe_uuid)

        if self.args.force:
            universe_delete_url += '?isForceDelete=true'
        universe_json = self.__call_api(universe_delete_url, is_delete=True)
        task_id = universe_json['taskUUID']

        print('Universe delete requested successfully. '
              'Use {0} as task id to get status of universe.'.format(task_id))
        if not self.args.no_wait:
            self.__task_progress_bar(task_id)

    def __validate_universe_name(self, universe_name):
        """
        Validate universe name is unique and allow create universe.

        :param universe_name: Name of the universe to take a json data from.
        :return: None
        """
        find_universe_url = '{0}/api/v1/customers/{1}/universes/find?name={2}'.format(
            self.base_url, self.customer_uuid, universe_name)

        response = self.__call_api(find_universe_url)
        if len(response) > 0:
            message = "Universe with name {0} already exists.".format(universe_name)
            raise ValueError(message)

    def create_universe(self):
        """
        Create the universe using the json and provided universe name.

        :return: None
        """
        if not self.args.file:
            sys.stderr.write("Input json file required. Use "
                             "'-f|--file <file_path>' to pass json input file.")
            sys.exit(1)
        input_file = os.path.join(SCRIPT_PATH, self.args.file)
        universe_name = self.args.universe_name

        data = {}
        with open(input_file) as file_object:
            data = convert_unicode_json(json.loads(file_object.read()))

        configure_json, universe_name = self.__modify_universe_config(data, universe_name)

        self.__validate_universe_name(universe_name)

        universe_config_json = self.__post_universe_config(configure_json)
        # add the missing fields in the config json.
        universe_config_json['clusterOperation'] = 'CREATE'
        universe_config_json['currentClusterType'] = 'PRIMARY'
        # Call universe config api to get the update config data.
        universe_config_json = self.__post_universe_config(universe_config_json)
        task_id = self.__create_universe_from_config(universe_config_json)
        print('Universe create requested successfully. '
              'Use {0} as task id to get status of universe.'.format(task_id))
        if not self.args.no_wait:
            self.__task_progress_bar(task_id)

    def get_single_customer_uuid(self):
        """
        Get customer UUID. Raise error in case multiple customer is present.

        :return: None
        """
        if self.args.customer_uuid:
            self.customer_uuid = self.args.customer_uuid
        else:
            customer_url = '{0}/api/v1/customers'.format(self.base_url)
            data = self.__call_api(customer_url)
            if len(data) == 1:
                self.customer_uuid = str(data[0].get("uuid"))
            else:
                raise ValueError('Muliple customer UUID present, Please provide customer UUID')

    def get_regions_data(self):
        """
        Get all region names and UUID with zones.

        :return: All available regions
        """
        provider_url = '{0}/api/v1/customers/{1}/regions'.format(self.base_url, self.customer_uuid)
        region_data = self.__call_api(provider_url)
        regions = []
        for each_region in region_data:
            zones = []
            for each_zone in each_region.get('zones'):
                zones.append({
                    'zone_name': each_zone.get('name'),
                    'zone_uuid': each_zone.get('uuid')
                })
            regions.append({
                'zones': zones,
                'region_name': each_region.get('name'),
                'region_uuid': each_region.get('uuid'),
                'provider': each_region.get('provider').get('code')
            })
        print(json.dumps(regions))

    def get_provider_data(self):
        """
        Get all providers names and UUID.

        :return: All available providers
        """
        provider_url = '{0}/api/v1/customers/{1}/providers'.format(
            self.base_url, self.customer_uuid)
        provider_data = self.__call_api(provider_url)
        providers = []
        for each_provider in provider_data:
            providers.append({
                'name': each_provider.get('name'),
                'uuid': each_provider.get('uuid')
            })
        print(json.dumps(providers))

    def delete_universe(self):
        """
        Function to do delete universe action.

        :return: None
        """
        confirmation = self.args.yes
        if not confirmation:
            user_input = get_input_from_user("Continue with deleting universe(y/n)?: ")
            confirmation = user_input.lower()[0] == 'y'
        if confirmation:
            if not self.args.force:
                print('\nNote:- Universe deletion can fail due to errors, '
                      'Use `--force` to ignore errors and force delete.\n')
            name = self.args.universe_name
            universe_uuid = self.args.universe_uuid
            if not (name or universe_uuid):
                sys.stderr.write("Required universe name | uuid to delete universe.")
                sys.stderr.write("\nUse '-n|--universe_name <universe_name>' "
                                 "to pass universe name.")
                sys.stderr.write("\nUse '-u|--universe_uuid <universe_uuid>' "
                                 "to pass universe uuid.")
                sys.exit(1)

            if self.args.universe_name:
                universe_uuid = self.__get_universe_uuid_by_name(self.args.universe_name)
            self.__delete_universe_by_uuid(universe_uuid)
        else:
            print("Aborted Delete universe")
            sys.exit()

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
        Function to task status action.

        :return: None
        """
        if not self.args.task:
            sys.stderr.write("Task id required to get task status. "
                             "Use '-t|--task <task_id>' to pass task_id.")
            sys.exit(1)
        self.__task_progress_bar(self.args.task)

    def __parse_arguments(self):
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
                    python yb_platform_util.py create_universe -f test-universe.json -n new-name

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
        parser.add_argument('actions', type=str, nargs=1, help=HelpMessage['ACTION'].value)
        parser.add_argument('-c', '--customer_uuid', help=HelpMessage['CUSTOMER_UUID'].value)
        parser.add_argument('-n', '--universe_name', help=HelpMessage['UNIVERSE_NAME'].value)
        parser.add_argument('-u', '--universe_uuid', help=HelpMessage['UNIVERSE_UUID'].value)
        parser.add_argument('-f', '--file', help=HelpMessage['FILE'].value,
                            metavar='INPUT_FILE_PATH')
        parser.add_argument('-t', '--task', help=HelpMessage['TASK'].value,
                            metavar='TASK_ID')
        parser.add_argument('-y', '--yes', action='store_true', help=HelpMessage['YES'].value)
        parser.add_argument('--interval', type=check_positive, default=20,
                            help=HelpMessage['INTERVAL'].value, metavar='INTEGER_INTERVAL')
        parser.add_argument('--no_wait', action='store_true', help=HelpMessage['NO_WAIT'].value)
        parser.add_argument('--force', action='store_true', help=HelpMessage['FORCE'].value)
        self.args = parser.parse_args()
        self.__parser = parser

    def invalid_action(self):
        """
        Hendle invalid action passed.

        :return: None
        """
        sys.stderr.write("Invalid action")
        self.__parser.print_help()

    def validate_env_values(self):
        """
        Validate if both env variable has been set.

        :return: None
        """
        if not self.base_url:
            sys.stderr.write("\n\nYB_PLATFORM_URL is not set. "
                             "Set YB_PLATFORM_URL as env variable to proceed.")
            sys.stderr.write("\n\texport YB_PLATFORM_URL=<base_url_of_platform>\n\nExample:")
            sys.stderr.write("\n\texport YB_PLATFORM_URL=http://localhost:9000\n\n")
        if not self.api_token:
            sys.stderr.write("YB_PLATFORM_API_TOKEN is not set. "
                             "Set YB_PLATFORM_API_TOKEN as env variable to proceed.")
            sys.stderr.write("\n\texport YB_PLATFORM_API_TOKEN=<platform_api_token>\n\nExample:")
            sys.stderr.write("\n\t export YB_PLATFORM_API_TOKEN="
                             "e16d75cf-79af-4c57-8659-2f8c34223551\n")
        if not (self.base_url or self.api_token):
            sys.exit(1)

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
