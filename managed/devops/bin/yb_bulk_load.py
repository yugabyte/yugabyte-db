#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

"""
Usage:  yb_bulk_load.py --key_path=SSH_KEY_PATH [--instance_count=INSTANCES] --universe=UNIVERSE
                        [--release=RELEASE] --masters=YB_MASTERS --table=TABLE --keyspace=KEYSPACE
                        --s3bucket=S3BUCKET [--run_step_only] [--cluster_id=CLUSTERID]
        yb_bulk_load.py (-h | --help)

Options:
.  -h --help
.  --key_path=SSH_KEY_PATH    ssh key name used for production YB universe.
.  --instance_count=INSTANCES number of instances to use for the MR job.
.  --universe=UNIVERSE        name of the YB universe (needs to be yb-N-<universe_name> from
                              portal).
.  --release=RELEASE          path to the YB release tar gz to use.
.  --masters=YB_MASTERS       comma separated list of YB masters.
.  --table=TABLE              table to bulk import data into.
.  --keyspace=KEYSPACE        keyspace for the table.
.  --s3bucket=S3BUCKET        s3 bucket to use for the MapReduce job.
                              s3bucket/input is where the input csv files should be uploaded.
                              s3bucket/output is the job output (will be created by EMR).
                              s3bucket/logs is where the logs will be stored.
.  --run_step_only            only run the MR step for the given cluster_id which is provisioned.
.  --cluster_id               cluster_id for the EMR cluster, to be used only with --run_step_only
"""

from docopt import docopt
import subprocess
import boto3
import json
import os
import sys
import time
import datetime

# Try to read home dir from environment variable, else assume it's /home/yugabyte.
YB_HOME_DIR = os.environ.get("YB_HOME_DIR", "/home/yugabyte")
YB_BIN_DIR = '%s/bin' % (YB_DIR)
HADOOP_SSH_DIR = '/home/hadoop/.ssh'
MAPPER = '%s/yb-generate_partitions_main' % (YB_BIN_DIR)
REDUCER = '%s/yb-bulk_load' % (YB_BIN_DIR)
START_TIME = datetime.datetime.now().replace(microsecond=0).isoformat()


def build_emr_cluster(emr_client, universe, instance_count, s3logs, subnet_id, security_group,
                      key_name):
    mapreduce_properties = {
        'mapreduce.reduce.speculative': 'false',
        'mapreduce.task.timeout': '6000000',
        'mapreduce.reduce.memory.mb': '53248',
        'mapred.tasktracker.reduce.tasks.maximum': '1'
    }
    job_flow_resp = emr_client.run_job_flow(
        Name='YB Bulk Load {} - {}'.format(universe, START_TIME),
        ReleaseLabel='emr-5.7.0',
        LogUri=s3logs,
        Instances={
            'InstanceGroups': [
                {
                    'Name': 'master',
                    'InstanceRole': 'MASTER',
                    'InstanceType': 'c3.2xlarge',
                    'InstanceCount': 1,
                    'Configurations': [
                        {
                            'Classification': 'mapred-site',
                            'Properties': mapreduce_properties
                        }
                    ]
                },
                {
                    'Name': 'core',
                    'InstanceRole': 'CORE',
                    'InstanceType': 'c3.8xlarge',
                    'InstanceCount': 1,
                    'Configurations': [
                        {
                            'Classification': 'mapred-site',
                            'Properties': mapreduce_properties
                        }
                    ]
                },
                {
                    'Name': 'task',
                    'InstanceRole': 'TASK',
                    'InstanceType': 'c3.8xlarge',
                    'InstanceCount': instance_count,
                    'Configurations': [
                        {
                            'Classification': 'mapred-site',
                            'Properties': mapreduce_properties
                        }
                    ]
                },
            ],
            'Ec2SubnetId': subnet_id,
            'EmrManagedMasterSecurityGroup': security_group,
            'EmrManagedSlaveSecurityGroup': security_group,
            'KeepJobFlowAliveWhenNoSteps': True,
            'Ec2KeyName': key_name
        },
        JobFlowRole='EMR_EC2_DefaultRole',
        ServiceRole='EMR_DefaultRole',
        VisibleToAllUsers=True,
        Tags=[{
            'Key': 'Name',
            'Value': '{}-{}-emr'.format(universe, START_TIME)
        }]
    )
    cluster_id = job_flow_resp['JobFlowId']

    print("EMR Job Flow response:")
    print(json.dumps(job_flow_resp, indent=4, sort_keys=True))

    # Wait for the cluster to come up.
    print("Waiting for cluster %s" % (cluster_id))
    waiter = emr_client.get_waiter('cluster_running')
    waiter.wait(ClusterId=cluster_id)
    print("Cluster %s is ready!" % (cluster_id))
    return cluster_id


def provision_emr_machines(emr_client, cluster_id, key_path, release_path, instance_count):

    print("Waiting for all instances to be in the running state")
    instances_running = 0
    instances = None
    # Wait for all instances to be up.
    while instances_running != instance_count + 2:
        # +2 for master and core.
        instances = emr_client.list_instances(
            ClusterId=cluster_id,
            InstanceGroupTypes=[
                'MASTER', 'CORE', 'TASK',
            ],
            InstanceStates=[
                'RUNNING',
            ],
        )
        instances_running = len(instances['Instances'])
        time.sleep(10)

    print("All instances are running!")

    # Provision software on all machines.
    for instance in instances['Instances']:
        private_ip = instance['PrivateIpAddress']
        print('Provisioning instance: %s' % (private_ip))
        # Create appropriate directories.
        subprocess.check_call(hadoop_master_ssh_cmd(key_path, private_ip,
                                                    'sudo mkdir -p %s' % (YB_DIR)))

        subprocess.check_call(hadoop_master_ssh_cmd(key_path, private_ip,
                                                    'sudo chown hadoop:hadoop %s' % (YB_DIR)))

        # Copy release targz and postprocess.
        release_file = os.path.basename(release_path)
        subprocess.check_call(hadoop_master_rsync_cmd(key_path, private_ip, release_path, YB_DIR))
        subprocess.check_call(hadoop_master_ssh_cmd(
            key_path, private_ip,
            'tar -zxvf %s -C %s --strip-components=1' %
            (os.path.join(YB_DIR, release_file), YB_DIR)))
        subprocess.check_call(hadoop_master_ssh_cmd(key_path, private_ip,
                                                    '%s/bin/post_install.sh' % (YB_DIR)))


def retrieve_subnet_security_group(universe):
    client = boto3.client('ec2')
    reservations = client.describe_instances(Filters=[
        {
            'Name': 'tag:Name',
            'Values': [universe + '*']
        }
    ])['Reservations']

    if len(reservations) < 1:
        raise Exception('Could not find any valid reservations for universe %s' % (universe))

    for reservation in reservations:
        instances = reservation['Instances']
        if len(instances) > 0 and instances[0]['State']['Name'] == 'running':
            subnet_id = instances[0]['SubnetId']
            security_groups = instances[0]['SecurityGroups']
            if len(security_groups) < 1:
                raise Exception('Couldn\'t find a valid security group for universe %s' % universe)
            return (subnet_id, security_groups[0]['GroupId'])

    raise Exception('Could not find any valid instances for universe %s' % (universe))


def run_bulk_load_step(emr_client, key_file, masters, table, keyspace, universe, s3bucket,
                       cluster_id):
    s3input = os.path.join(s3bucket, "input")
    s3output = os.path.join(s3bucket, "output", START_TIME)

    # Build a comma separated list for the -files parameter.
    remote_ssh_key = os.path.join(HADOOP_SSH_DIR, key_file)
    hadoop_files = '%s,%s/bulk_load_helper.sh,%s/bulk_load_cleanup.sh' % (
        remote_ssh_key, YB_BIN_DIR, YB_BIN_DIR)

    # Build mapper and reducer cmds.
    mapper_cmd = MAPPER + ' --master_addresses ' + masters + ' --table_name ' + table +\
        ' --namespace_name ' + keyspace + ' --logtostderr=1'
    reducer_cmd = REDUCER + ' --master_addresses ' + masters + ' --table_name ' + table +\
        ' --namespace_name ' + keyspace + ' --logtostderr=1' + ' --base_dir /tmp/ ' +\
        '--ssh_key_file ' + key_file + ' --export_files true --initial_seqno 0 ' +\
        '--memtable_size_bytes 12884901888 --logtostderr=1 --bulk_load_num_memtables 3 ' +\
        '--bulk_load_num_threads 32 --rocksdb_level0_stop_writes_trigger 100 ' +\
        '--rocksdb_level0_slowdown_writes_trigger 100'

    # Submit the step to the cluster.
    step_response = emr_client.add_job_flow_steps(
        JobFlowId=cluster_id,
        Steps=[{
            'Name': 'YB Bulk Load Step for {} - {}'.format(universe, START_TIME),
            'ActionOnFailure': 'CONTINUE',
            'HadoopJarStep': {
                'Jar': 'command-runner.jar',
                'Args': [
                    'hadoop-streaming',
                    '-files', hadoop_files,
                    '-mapper', mapper_cmd,
                    '-reducer', reducer_cmd,
                    '-input', s3input,
                    '-output', s3output
                ]
            }
        }],
    )
    step_id = step_response['StepIds'][0]

    print("Submitted bulk load step %s to cluster" % (step_id))
    print("Waiting for bulk load step to finish...")

    # Wait for the step to complete.
    step_completed = False
    while not step_completed:
        step_status = emr_client.describe_step(ClusterId=cluster_id, StepId=step_id)
        step_state = step_status['Step']['Status']['State']
        if step_state == 'COMPLETED':
            step_completed = True
        elif step_state in ('CANCEL_PENDING', 'CANCELLED', 'FAILED', 'INTERRUPTED'):
            raise Exception("Step %s failed with state: %s" % (step_id, step_state))
        time.sleep(60)

    print("Completed bulk load step %s" % (step_id))


def run_bulk_load(universe, instance_count, key_path, release_path, masters, table, keyspace,
                  s3bucket, run_step_only, cluster_id):
    (subnet_id, security_group) = retrieve_subnet_security_group(universe)

    s3logs = os.path.join(s3bucket, "logs")

    # Process ssh key file to get key file and key name.
    print("SSH key path: %s" % (key_path))
    path, extension = os.path.splitext(key_path)
    key_file = os.path.basename(key_path)
    key_name = os.path.basename(path)
    print("SSH key name: %s" % (key_name))

    # Get an emr_client.
    emr_client = boto3.client('emr')

    if not run_step_only:
        instance_count = int(instance_count)
        # Build the cluster.
        cluster_id = build_emr_cluster(emr_client, universe, instance_count, s3logs, subnet_id,
                                       security_group, key_name)

        # Provision all the emr machines.
        provision_emr_machines(emr_client, cluster_id, key_path, release_path, instance_count)

    # Get the master.
    master_instance = emr_client.list_instances(
        ClusterId=cluster_id,
        InstanceGroupTypes=[
            'MASTER',
        ],
    )
    master_private_ip = master_instance['Instances'][0]['PrivateIpAddress']

    # Copy over the ssh file to the master.
    subprocess.check_call(hadoop_master_rsync_cmd(key_path, master_private_ip, key_path,
                                                  HADOOP_SSH_DIR))

    # Run the MapReduce step for bulk_load.
    run_bulk_load_step(emr_client, key_file, masters, table, keyspace, universe, s3bucket,
                       cluster_id)

    # Terminate the cluster.
    emr_client.terminate_job_flows(
        JobFlowIds=[
            cluster_id,
        ]
    )

    print('Terminated cluster %s' % (cluster_id))


def hadoop_master_ssh_cmd(key_path, master_host, cmd):
    return ['ssh', '-o', 'StrictHostKeyChecking=no', '-i', key_path, 'hadoop@%s' % master_host, cmd]


def hadoop_master_rsync_cmd(key_path, master_host, src, dest):
    return ['rsync', '-e', 'ssh -o StrictHostKeyChecking=no -i ' + key_path, src,
            'hadoop@%s:%s' % (master_host, dest)]


if __name__ == '__main__':
    args = docopt(__doc__)
    run_step_only = args['--run_step_only']
    cluster_id = args['--cluster_id']
    if run_step_only and not cluster_id:
        print("Need to specify --cluster_id with --run_step_only")
        sys.exit(1)

    run_bulk_load(
        args['--universe'], args['--instance_count'], args['--key_path'],
        args['--release'], args['--masters'], args['--table'], args['--keyspace'],
        args['--s3bucket'], run_step_only, cluster_id)
