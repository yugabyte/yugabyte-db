#!/usr/bin/env python

import argparse
import json

import logging
import os
import subprocess
import sys
import tempfile

# Logging
log = logging.getLogger("edit-provider")
log.addHandler(logging.StreamHandler(stream=sys.stdout))
log.setLevel(logging.INFO)

# Constants
VALID_PROVIDER_CODES = ["gcp"]

class KubeInfo:
    def __init__(self, pod, namespace=None, kubeconfig=None):
        self.pod = pod
        self.namespace = namespace
        self.kubeconfig = kubeconfig

# Values to be set at initialization
kubeInfo = None
jarName = "configencrypt-1.0.jar"


def _run_psql_query_on_k8s(query, raise_on_failure=True):
    psql_cmd = ["psql", "-qtAX", "-U", "postgres", "-d", "yugaware", "-h", "localhost", "-c", query]
    outer_cmd = []
    if kubeInfo:
        outer_cmd = ["kubectl"]
        if kubeInfo.namespace:
            outer_cmd.extend(["--namespace", kubeInfo.namespace])
        if kubeInfo.kubeconfig:
            outer_cmd.extend(["--kubeconfig", kubeInfo.kubeconfig])
        outer_cmd.extend(["exec", kubeInfo.pod, "-c", "postgres", "--"])
    else:
        log.fatal("unknown execute type")
    cmd = outer_cmd + psql_cmd
    log.debug("running psql command: {}".format(cmd))
    proc = subprocess.run(cmd, capture_output=True)
    if raise_on_failure:
        proc.check_returncode()
    return proc.stdout.strip()

def list_providers():
    class ListProvidersOutput:
        def __init__(self, raw):
            self._raw_string = raw.decode('utf-8')

        def table_str(self):
            rows = ["{:37} | {:4}".format("uuid", "name")]
            rows.append('-'*44)
            for d in self._raw_string.splitlines():
                if not d.strip():
                    continue
                name, uuid = d.split('|', 1)
                rows.append("{:37} | {}".format(uuid.strip(), name.strip()))
            return '\n'.join(rows)

    log.debug("listing providers")
    query = "SELECT name, uuid FROM provider;"
    output = _run_psql_query_on_k8s(query)
    return ListProvidersOutput(output).table_str()

def get_provider_code(p_uuid):
    query = "SELECT code FROM provider WHERE uuid='{}'".format(p_uuid)
    output = _run_psql_query_on_k8s(query)
    return output.decode()

def run_decrypt(p_uuid, customer_uuid):
    query = "SELECT config->'encrypted' FROM provider WHERE uuid='{}'".format(p_uuid)
    b_output = _run_psql_query_on_k8s(query)
    encrypted = b_output.decode().strip('"')


    cmd = ["java", "-jar", jarName, "-c", customer_uuid, "-e", encrypted]
    proc = subprocess.run(cmd, capture_output=True)
    proc.check_returncode()
    return proc.stdout.decode()

def run_encrypt(plain, customer_uuid):

    cmd = ["java", "-jar", jarName, "-c", customer_uuid, "-j", plain]
    proc = subprocess.run(cmd, capture_output=True)
    proc.check_returncode()
    return proc.stdout.decode()

def edit_data(orig_str):
    (_, edit_file_path) = tempfile.mkstemp(prefix="edit")
    with open(edit_file_path, "w") as edit_file:
        edit_file.write(orig_str)

    (_, orig_file_path) = tempfile.mkstemp(prefix="orig")
    with open(orig_file_path, "w") as orig_file:
        orig_file.write(orig_str)

    os.system("${EDITOR-vim} \"" + edit_file_path + "\"")
    ret_code = os.system("diff {} {}".format(orig_file_path, edit_file_path))
    if 0 == ret_code:
        print("No changes made to universe json.")
        sys.exit(0)

    input_method = input if sys.version_info.major == 3 else raw_input

    confirm = input_method("Confirm the changes above by typing yes: ")
    if confirm != 'yes':
        print("No changes made to database.")
        sys.exit(0)

    with open(edit_file_path, 'r') as edit_file:
        return edit_file.read()

def update_provider_credentials(p_uuid):
    log.debug("starting provider update")
    code = get_provider_code(p_uuid)
    log.info("got provider code {}".format(code))
    if code not in VALID_PROVIDER_CODES:
        log.fatal("invalid provider {}".format(code))

    query = "SELECT customer_uuid FROM provider WHERE uuid='{}'".format(p_uuid)
    b_output = _run_psql_query_on_k8s(query)
    cUUID = b_output.decode()
    log.info("provider was created by customer {}".format(cUUID))

    decrypted = run_decrypt(p_uuid, cUUID)
    parsed = json.loads(decrypted)
    pretty_json = json.dumps(parsed, indent=4)
    updated = edit_data(pretty_json)
    updated_parsed = json.loads(updated)

    encrypted = run_encrypt(json.dumps(updated_parsed), cUUID)
    query = "UPDATE provider set config='{}' WHERE uuid='{}'".format(encrypted, p_uuid)
    log.info("updating postgres")
    _run_psql_query_on_k8s(query)
    log.info("successfully updated postgres")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    install_types = ["kubernetes"]
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("-t", "--install_type", choices=install_types, default=install_types[0],
                    help="Type of installation.", required=False)

    parser.add_argument("-j", "--jar-file", help="path to the encryptor jar file", dest="jar")

    # Kubernetes values
    parser.add_argument("-p", "--pod", help="Name of YugabyteDB Anywhere pod for kubernetes install types", required=True)
    parser.add_argument("-n", "--namespace", help="Kubernetes namespace.", required=False)
    parser.add_argument("-k", "--kubeconfig", help="Kubernetes kube config filepath.", required=False)

    parser.add_argument("-u", "--provider-uuid", help="uuid of the provider to update", dest="provider")

    parser.add_argument("--list-providers", action="store_true", dest="list")
    args = parser.parse_args()

    if args.verbose:
        log.setLevel(logging.DEBUG)

    if args.jar:
        log.debug("setting jarfile to {}".format(args.jar))
        jarName = args.jar

    if args.install_type == "kubernetes":
        kubeInfo = KubeInfo(args.pod, args.namespace, args.kubeconfig)

    if args.list:
        print(list_providers())
        sys.exit(0)

    if not args.provider:
        log.fatal("no provider uuid given")

    update_provider_credentials(args.provider)
