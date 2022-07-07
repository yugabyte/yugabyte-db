#!/usr/bin/env python
import argparse
import json
import subprocess
import os
import sys
import tempfile


def msg_exit(msg):
    print(msg)
    sys.exit(0)


def get_kubectl_cmd_prefix():
    kubectl_cmd = ['kubectl']
    if args.namespace:
        kubectl_cmd.extend(['-n', args.namespace])
    if args.kubeconfig:
        kubectl_cmd.extend(['--kubeconfig', args.kubeconfig])
    return kubectl_cmd


def get_running_pod(kubectl_cmd_prefix):
    pod_cmd = ' '.join(kubectl_cmd_prefix) + " get pods | awk '($3 ~\"Running\"){print $1; exit}'"
    # Run in shell because of pipe operation.
    pod_name = str(subprocess.check_output(pod_cmd, shell=True).decode('utf-8')).strip()
    if not pod_name:
        msg_exit("No running pod is found.")
    print("Found running pod: {}".format(pod_name))
    return pod_name


def run_psql(psql_query):
    psql_cmd = None
    if args.install_type == "standalone":
        psql_cmd = ["psql"]
    elif args.install_type == "docker":
        if os.system("sudo docker ps -a | grep postgres > /dev/null") != 0:
            msg_exit("postgres docker container is not running.")
        psql_cmd = "sudo docker exec postgres psql".split(" ")
    elif args.install_type == "kubernetes":
        kubectl_cmd_prefix = get_kubectl_cmd_prefix()
        pod_name = args.pod
        if not pod_name:
            pod_name = get_running_pod(kubectl_cmd_prefix)
        psql_cmd = kubectl_cmd_prefix + ['exec', pod_name, '-t', '-c', 'postgres', '--', "psql"]
    else:
        msg_exit("Unknown install type: {}.".format(args.install_type))
    return str(subprocess.check_output(
        psql_cmd +
        ['-U', 'postgres', '-d', 'yugaware', '-h', 'localhost', '-t', '-c', psql_query])
            .decode('utf-8')).strip()


if os.path.exists("/.dockerenv"):
    msg_exit("It appears that this script is being run from within the yugaware docker container." +
             " Please copy it to the docker host and run it from the docker host.")

# Add arguments for parsing.
install_types = ["standalone", "docker", "kubernetes"]
parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument("-i", "--universe_uuid", help="Universe UUID.", required=True)
parser.add_argument("-t", "--install_type", choices=install_types, default=install_types[0],
                    help="Type of installation.", required=False)
parser.add_argument("-n", "--namespace", help="Kubernetes namespace.", required=False)
parser.add_argument("-f", "--kubeconfig", help="Kubernetes kube config filepath.", required=False)
parser.add_argument("-p", "--pod", help="YugabyteDB Anywhere pod name.", required=False)
args = parser.parse_args()

univ_uuid = args.universe_uuid
json_text = run_psql("select universe_details_json from universe " +
                     "where universe.universe_uuid='{}';".format(univ_uuid))
if not json_text:
    msg_exit("Universe {} does not exist.".format(univ_uuid))
json_parsed = json.loads(json_text)
json_pretty = json.dumps(json_parsed, indent=4, sort_keys=True)


(_, edit_file_path) = tempfile.mkstemp(prefix="edit")
with open(edit_file_path, "w") as edit_file:
    edit_file.write(json_pretty)

(_, orig_file_path) = tempfile.mkstemp(prefix="orig")
with open(orig_file_path, "w") as orig_file:
    orig_file.write(json_pretty)

os.system("${EDITOR-vim} \"" + edit_file_path + "\"")
ret_code = os.system("diff {} {}".format(orig_file_path, edit_file_path))
if 0 == ret_code:
    msg_exit("No changes made to universe json.")

input_method = input if sys.version_info.major == 3 else raw_input

confirm = input_method("Confirm the changes above by typing yes: ")
if confirm != 'yes':
    msg_exit("No changes made to database.")

with open(edit_file_path, "r") as edit_file:
    new_json_parsed = json.loads(edit_file.read())

print("Updating universe json")
print(run_psql(("update universe set universe_details_json='{}' " +
               "where universe.universe_uuid='{}';").format(
                    json.dumps(new_json_parsed),
                    univ_uuid)))
