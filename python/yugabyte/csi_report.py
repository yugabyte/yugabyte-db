# Library for test reporting to YB Report Portal server.

import json
import logging
import os
from typing import List, Tuple, Any, Dict
from yugabyte.test_descriptor import TestDescriptor, SimpleTestDescriptor

# Non-standard module. Needed in builddir venv for code-checks and in system modules for spark job
# submission and spark test execution.
import requests

# Confirgurable Environment =========
# YB_CSI_LAUNCH - Launch ID, if blank, no reporting is done and no other EV need be set.
# CSI_API       - API URL (required)
# CSI_TOKEN     - credential token (required)
# YB_CSI_MAX_LOGMSG - Integer in Bytes - Larger files will be uploaded as attachment
# YB_CSI_MAX_FILE - Integer in Bytes - Max attachment size (limited by CSI service)
# ===================================
# Set by caller (run_tests_on_spark.py), based on other criteria
# YB_CSI_REPS   - integer (default: 1) Number of times each test is being run.
# ===================================
# Returned by create_suite(), but put into environment by caller:
# YB_CSI_C++    - Suite ID for test, by language
# YB_CSI_Java


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
    # For form data, we do not specify content, and let requests module set it
    # via use of files parameter.
    if content == 'json':
        csi_dict['headers']['Content-Type'] = 'application/json'

    return csi_dict


# Convert floating-point seconds epoch time to integer milliseconds
def mst(time_sec: float) -> int:
    return round(time_sec * 1000)


# Current practice is to name suite by language, return an EV name/value pair, that can be
# looked up on worker nodes via EV for each test.
# Since individual tests are run in distributed/parallel fashion, the suites must be created before
# the tests are launched, and IDs passed via environment.
# If this is changed so that each test class is a different suite, we might want to pass the list in
# a file rather than by environment variables. Another option would be to run an entire suite as a
# single spark task instead of single tests, but that requires larger re-factor.
def create_suite(suite_name: str, parent: str, method: str, planned: int, reps: int,
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
        'attributes': [{'key': method, 'value': planned}],
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


def create_test(test: TestDescriptor, time_sec: float, attempt: int) -> str:
    csi = csi_env()
    parent = os.getenv('YB_CSI_' + test.language, '')

    if not csi['launch'] or not parent:
        return ''

    if attempt > 0:
        # Spark re-tries happen only if there is some failure, so attempts are serial.
        retry = True
        # In this case the previous attempt died before having a chance to report completion.
        # If we had the ID of the previous attempt, we could report it as interrupted, but we are
        # doing asynchronous reporting, so query to CSI server may not find it.
        # So the first complete attempt (pass/fail) will show as result for all the prior attempts.
    else:
        # Repetitions are same test intentionally run multiple times, in parallel and random order.
        # So we need to mark them all (even test.attempt_index 0) as retry.
        # Only in case in which repetitions == 1 and spark attempt == 0 is retry=False.
        retry = (csi['reps'] != '1')

    full_name = test.descriptor_str_without_attempt_index
    pt = SimpleTestDescriptor.parse(full_name)
    tname = f"{pt.class_name} - {pt.test_name}"

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
def close_item(item: str, time_sec: float, status: str, tags: List[str]) -> str:
    csi = csi_env()
    if not csi['launch'] or not item:
        return ''
    req_data = {
        'launchUuid': csi['launch'],
        'endTime': mst(time_sec),
        'attributes': tags
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
    msg_limit = int(os.getenv('YB_CSI_MAX_LOGMSG', '50000'))  # 50K
    file_limit = int(os.getenv('YB_CSI_MAX_FILE', '104857600'))  # 100MB
    failed_num = 0
    for path in path_list:
        large_file = False
        base_file = os.path.basename(path)

        if os.path.exists(path):
            if path.endswith('.gz'):
                upload_attachment(item, time_sec, base_file, path)
                continue
            file_size = os.path.getsize(path)
            try:
                with open(path, 'r') as file:
                    if file_size > msg_limit:
                        log_content = base_file
                        if file_size < file_limit:
                            log_content += ' [TRUNCATED end of file] (full file attached) =====\n'
                            large_file = True
                        else:
                            log_content += ' [TRUNCATED end of file] (file too large) =====\n'
                            log_content += f" size: {file_size} limit: {file_limit} =====\n"
                        file.seek(file_size - msg_limit)
                        log_content += '  ...  ' + file.read()
                    else:
                        log_content = base_file + ' =====\n' + file.read()
            except Exception as e:
                logging.error(f"CSI Error: Could not read {path}: {e}")
                failed_num += 1
                continue
        else:
            logging.error(f"CSI log: No such file: {path}")
            failed_num += 1
            continue

        req_data = {
            'launchUuid': csi['launch'],
            'itemUuid': item,
            'time': mst(time_sec),
            'message': log_content
        }
        response = requests.post(csi['url'] + '/log',
                                 headers=csi['headers'],
                                 data=json.dumps(req_data))
        if response.status_code == 201:
            logging.info(f"CSI log: {base_file} " + response.json()['id'])
        else:
            logging.error(f"CSI Error({response.status_code}): Log of {path} failed: " +
                          response.text)
            failed_num += 1
        if large_file:
            upload_attachment(item, time_sec, base_file, path)
    return failed_num


def upload_attachment(item: str, time_sec: float, message: str, path: str) -> int:
    csi = csi_env('form')
    if not csi['launch'] or not item:
        return 0
    file_limit = int(os.getenv('YB_CSI_MAX_FILE', '104857600'))  # 100MB
    file_size = os.path.getsize(path)
    if file_size > file_limit:
        logging.error(f"CSI attach: {path} exceeds max size - Skipping")
        logging.error(f"CSI attach: file size: {file_size} limit: {file_limit}")
        return 1
    base_file = os.path.basename(path)
    req_data = [{
        'launchUuid': csi['launch'],
        'itemUuid': item,
        'time': mst(time_sec),
        'message': message,
        'file': {'name': base_file, 'contentType': 'text/plain'}
    }]
    file_data = (('file', (base_file, open(path, 'rb'))),
                 ('json_request_part', ('json_request_part', json.dumps(req_data),
                                        "application/json")))
    response = requests.post(csi['url'] + '/log',
                             headers=csi['headers'],
                             files=file_data)
    if response.status_code == 201:
        logging.info(f"CSI attach: {base_file} {response.text}")
        return 0
    else:
        logging.error(f"CSI Error({response.status_code}): Attachment of {path} failed: " +
                      response.text)
        return 1
