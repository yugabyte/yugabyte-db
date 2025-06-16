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
# YB_CSI_LID    - Launch UUID, if blank, no reporting is done and no other EV need be set.
# CSI_SERVER    - Host name to report to (required)
# CSI_PROJ      - Project name to report to (required)
# CSI_TOKEN     - credential token (required)
# YB_CSI_MAX_LOGMSG - Integer in Bytes - Larger files will be uploaded as attachment
# YB_CSI_MAX_FILE - Integer in Bytes - Max attachment size (limited by CSI service)
# ===================================
# Set by caller (run_tests_on_spark.py), based on other criteria
# YB_CSI_REPS   - integer (default: 1) Number of times each test is being run.
# YB_CSI_QID    - integer ID of launch to query
# ===================================
# Returned by create_suite(), but put into environment by caller:
# YB_CSI_C++    - Suite ID for test, by language
# YB_CSI_Java


# API & Token should be set via jenkins jobs,
# but launch is only set if CSI reporting is enabled.
# If launch is set, we can assume the others are as well.
def csi_env(content: str = 'json') -> Dict[str, Any]:

    server = os.getenv('CSI_SERVER', '')
    project = os.getenv('CSI_PROJ', '')

    csi_dict: Dict[str, Any]
    csi_dict = {
        'launch': os.getenv('YB_CSI_LID', ''),
        'url': f"https://{server}/api/v2/{project}",
        'url_sync': f"https://{server}/api/v1/{project}",
        'reps': os.getenv('YB_CSI_REPS', '1'),
        'qid': os.getenv('YB_CSI_QID', ''),
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


# Find the physical ID from the UUID. Required for querying prior test data.
def launch_qid() -> str:
    csi = csi_env()
    if not csi['launch']:
        return ''
    q_id = ''
    response = requests.get(csi['url_sync'] + '/launch/' + csi['launch'], headers=csi['headers'])
    if response.status_code == 200:
        q_id = response.json()['id']
    else:
        logging.error(f"CSI Error: Launch {csi['launch']} not found. {response.text}")

    logging.info(f"CSI Launch Query ID: {q_id}")
    return str(q_id)


# Current practice is to name suite by language, return an EV name/value pair, that can be
# looked up on worker nodes via EV for each test.
# Since individual tests are run in distributed/parallel fashion, the suites must be created before
# the tests are launched, and IDs passed via environment.
# If this is changed so that each test class is a different suite, we might want to pass the list in
# a file rather than by environment variables. Another option would be to run an entire suite as a
# single spark task instead of single tests, but that requires larger re-factor.
def create_suite(qid: str, suite_name: str, parent: str, method: str, planned: int, reps: int,
                 time_sec: float) -> Tuple[str, str]:
    csi = csi_env()
    varname = 'YB_CSI_' + suite_name
    # If we have a launch, it is still possible creation of parent failed.
    if not csi['launch'] or not parent:
        return (varname, '')
    suite_uuid = ''

    # Check if suite already exists from previous run
    if qid:
        query = {
            'filter.eq.launchId': qid,
            'filter.eq.name': suite_name,
            'filter.eq.type': 'SUITE',
            'page.size': '1'
        }
        response = requests.get(csi['url_sync'] + '/item',
                                headers=csi['headers'],
                                params=query)
        if response.status_code == 200:
            results = response.json()['content']
            if len(results) > 0:
                suite_id = results[0]['id']
                suite_uuid = results[0]['uuid']

    if suite_uuid:
        # Update attributes of existing suite
        up_data = {
            'attributes': [{'key': method, 'value': planned}],
        }
        if reps > 1:
            up_data['attributes'].append({'key': 'repititions', 'value': reps})
        response = requests.put(csi['url_sync'] + '/item/' + suite_id + '/update',
                                headers=csi['headers'],
                                data=json.dumps(up_data))
        if response.status_code == 200:
            logging.info(f"CSI update suite: Existing {suite_name} found")
        else:
            logging.error(f"CSI Error: Update of {suite_name} failed: {response.text}")
    else:
        # Create suite
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
            suite_uuid = response.json()['id']
        else:
            logging.error(f"CSI Error: Creation of {suite_name} failed: {response.text}")

    return (varname, suite_uuid)


def query_test(uniqueId: str, wait: bool) -> bool:
    csi = csi_env()
    found_prev = False

    # We are only waiting for a previous test to have started, but there may be a delay due to
    # asynchronous reporting.
    # Even if we think there should be a previous run, it may have died before reporting, so do not
    # wait more than 2 minutes.
    for wait_time in [5, 5, 10, 30, 60]:
        query = {
            'filter.eq.launchId': csi['qid'],
            'filter.eq.uniqueId': uniqueId,
            'page.size': '1'
        }
        response = requests.get(csi['url_sync'] + '/item',
                                headers=csi['headers'],
                                params=query)
        if response.status_code == 200:
            results = response.json()['content']
            if len(results) > 0:
                logging.info(f"CSI retry: Found previous test {results[0]['id']}")
                found_prev = True
                break
        if not wait:
            break

    return found_prev


# Normally, we want to use asynchronous reporting of results to not slow down test runs.
# For test re-try, though, if the prior attempt did not get reported, then the re-try report will
# fail and then there is no record. So we need to query to check for the prior attempt and in the
# case it might be in-process, wait for it, to be sure whether we shold report as a retry or not.
def create_test(test: TestDescriptor, time_sec: float, attempt: int) -> str:
    csi = csi_env()
    parent = os.getenv('YB_CSI_' + test.language, '')

    if not csi['launch'] or not parent:
        return ''

    full_name = test.descriptor_str_without_attempt_index
    pt = SimpleTestDescriptor.parse(full_name)
    tname = f"{pt.class_name} - {pt.test_name}"

    if attempt > 0:
        # Spark re-tries happen only if there is some failure, so attempts are serial.
        retry = True
        wait = True
        # In this case the previous attempt died before having a chance to report completion.
        # We need to query to find out if the previous attempt reported start or not.
    else:
        # Repetitions are same test intentionally run multiple times, in parallel and random order.
        # We need to query to make sure one has at least started before reporting these as retry.
        retry = (csi['reps'] != '1')
        if test.attempt_index == 1:
            wait = False
        else:
            wait = True

    if retry:
        prev_test = query_test(full_name, wait)
        if not prev_test:
            # Found no previous test, so do not call this one a retry.
            retry = False

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
    test_id = ''
    if response.status_code == 201:
        logging.info(f"CSI: Creation of {tname}")
        test_id = response.json()['id']
    else:
        logging.error(f"CSI Error: Creation of {tname}, failed: {response.text}")

    if attempt > 0:
        log_data = {
            'launchUuid': csi['launch'],
            'itemUuid': test_id,
            'time': mst(time_sec),
            'level': "info",
            'message': f"Spark Attempt {attempt}"
        }
        response = requests.post(csi['url'] + '/log',
                                 headers=csi['headers'],
                                 data=json.dumps(log_data))
        if response.status_code != 201:
            logging.error(f"CSI Error({response.status_code}): Log of spark attempt failed: " +
                          response.text)
    return test_id


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
    file_limit = int(os.getenv('YB_CSI_MAX_FILE', '67108864'))  # 64MB
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
    file_limit = int(os.getenv('YB_CSI_MAX_FILE', '67108864'))  # 64MB
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
