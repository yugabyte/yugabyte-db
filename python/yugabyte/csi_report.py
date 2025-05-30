# Library for test reporting to YB Report Portal server.

import json
import logging
import os
from typing import List, Tuple, Any, Dict
from yugabyte.test_descriptor import TestDescriptor, SimpleTestDescriptor

# Non-standard module. Needed in builddir venv for code-checks and in system modules for spark job
# submission and spark test execution.
import requests


# API & Token should be set via jenkins jobs,
# but launch is only set if CSI reporting is enabled.
# If launch is set, we can assume the other two are as well.
def csi_env(content: str = 'json') -> Dict[str, Any]:

    csi_dict: Dict[str, Any]
    csi_dict = {
        'launch': os.getenv('YB_CSI_LAUNCH', ''),
        'url': os.getenv('CSI_API', ''),
        'reps': os.getenv('YB_CSI_REPS', '1'),
        'headers': {
            'Authorization': 'Bearer ' + os.getenv('CSI_TOKEN', '')
        }
    }
    # For form date, we do not specify content, and let requests module set it
    # via use of files parameter.
    if content == 'json':
        csi_dict['headers']['Content-Type'] = 'application/json'

    return csi_dict


# Convert floating-point seconds epoch time to integer milliseconds
def mst(time_sec: float) -> int:
    return round(time_sec * 1000)


# Current practice is to name suite by language, return an EV name/value pair, that can be
# looked up on worker nodes via EV for each test.
def create_suite(suite_name: str, parent: str, planned: int, reps: int,
                 time_sec: float) -> Tuple[str, str]:
    csi = csi_env()
    varname = 'YB_CSI_' + suite_name
    # If we have a launch, it is still possible creation of parent failed.
    if not csi['launch'] or not parent:
        return (varname, '')

    req_data = {
        'name': suite_name,
        'launchUuid': csi['launch'],
        'type': 'suite',
        'attributes': [{'key': 'planned', 'value': planned}],
        'startTime': mst(time_sec)
    }
    if reps > 1:
        req_data['attributes'].append({'key': 'repititions', 'value': reps})
    response = requests.post(csi['url'] + '/item/' + parent,
                             headers=csi['headers'],
                             data=json.dumps(req_data))
    if response.status_code == 201:
        logging.info(f"CSI create suite: {suite_name} under {parent}")
        return (varname, response.json()['id'])
    else:
        logging.error(f"CSI Error: Creation of {suite_name} failed: {response.text}")
        return (varname, '')


def create_test(test: TestDescriptor, time_sec: float) -> str:
    csi = csi_env()
    parent = os.getenv('YB_CSI_' + test.language, '')

    if not csi['launch'] or not parent:
        return ''

    # We could set retry based on test.attempt_index, but first attempt is not distiguishable from a
    # run with no retries, and since the tries get shuffled and distributed, running a non-retry
    # after the retry=true will screw up reporting. So, based on environment var, if there are
    # multiple repetitions, mark them ALL as retries.
    retry = (csi['reps'] != '1')

    full_name = test.descriptor_str_without_attempt_index
    pt = SimpleTestDescriptor.parse(full_name)
    tname = f"{pt.class_name} - {pt.test_name}"

    print(f"Debug test: {test}")
    print(f"Debug parsed: {pt}")

    # TestCase ID (used to compare across test runs) seems to depend on codeRef & parameters
    # instead we give uniqueId with fully-specified name.  uniqueID is not shown in web UI.
    req_data = {
        'name': tname,
        'launchUuid': csi['launch'],
        'type': 'step',
        'uniqueId': full_name,
        'retry': retry,
        'startTime': mst(time_sec),
        'attributes': [
            {'key': 'class', 'value': pt.class_name},
            {'key': 'test',  'value': pt.test_name},
            {'key': 'lang',  'value': test.language}
        ]
    }
    response = requests.post(csi['url'] + '/item/' + parent,
                             headers=csi['headers'],
                             data=json.dumps(req_data))
    if response.status_code == 201:
        logging.info(f"CSI: Creation of {tname}")
        return response.json()['id']
    else:
        logging.error(f"CSI Error: Creation of {tname}, failed: {response.text}")
        return ''


# finish test/suite
def close_item(item: str, time_sec: float, status: str) -> str:
    csi = csi_env()
    if not csi['launch'] or not item:
        return ''
    req_data = {
        'launchUuid': csi['launch'],
        'endTime': mst(time_sec)
    }
    if status:
        req_data['status'] = status
        if status == 'skipped':
            # Do not mark skipped items as needing investigation.
            req_data['issue'] = {"issueType": "NOT_ISSUE"}
    logging.info("CSI close: " + item)
    response = requests.put(csi['url'] + '/item/' + item,
                            headers=csi['headers'],
                            data=json.dumps(req_data))
    if response.status_code == 200:
        logging.info("CSI close: " + response.json()['message'])
    else:
        logging.error(f"CSI Error: Report of {item} failed: {response.text}")
    return ''


def upload_log(item: str, time_sec: float, path_list: List[str]) -> int:
    csi = csi_env()
    if not csi['launch']:
        return 0
    failed_num = 0
    for path in path_list:
        if os.path.exists(path):
            try:
                with open(path, 'r') as file:
                    log_content = file.read()
            except Exception as e:
                logging.error(f"CSI Error: Could not read {path}: {e}")
        req_data = {
            'launchUuid': csi['launch'],
            'itemUuid': item,
            'time': mst(time_sec),
            'message': os.path.basename(path) + ' =====\n' + log_content
        }
        response = requests.post(csi['url'] + '/log',
                                 headers=csi['headers'],
                                 data=json.dumps(req_data))
        if response.status_code == 201:
            logging.info("CSI upload: " + response.json()['id'])
        else:
            logging.error(f"CSI Error: Log of {path} failed: {response.text}")
            failed_num += 1
    return failed_num


def upload_attachment(item: str, time_sec: float, message: str, file_path: str) -> int:
    csi = csi_env('form')
    if not csi['launch'] or not item:
        return 0
    base_file = os.path.basename(file_path)
    req_data = [{
        'launchUuid': csi['launch'],
        'itemUuid': item,
        'time': mst(time_sec),
        'message': message,
        'file': {'name': os.path.basename(file_path), 'contentType': 'text/plain'}
    }]
    file_data = (('file', (base_file, open(file_path, 'rb'))),
                 ('json_request_part', ('json_request_part', json.dumps(req_data),
                                        "application/json")))
    response = requests.post(csi['url'] + '/log',
                             headers=csi['headers'],
                             files=file_data)
    if response.status_code == 200:
        logging.info("CSI upload: " + response.json()['id'])
        return 0
    else:
        logging.error(f"CSI Error: Log of {file_path} failed: {response.text}")
        return 1
