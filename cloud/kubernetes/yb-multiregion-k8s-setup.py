#!/usr/bin/env python

# This script sets up a global DNS across 3 GKE clusters. This global DNS
# is leveraged by YugabyteDB to run a single multi-region globally-consistent
# database cluster. Detailed multi-cluster / multi-region YugabyteDB on Kubernetes docs
# are available at https://docs.yugabyte.com/latest/deploy/kubernetes/multi-cluster/gke/

from __future__ import print_function

import distutils.spawn
import json
import os

from subprocess import check_call, check_output
from sys import exit
from time import sleep

# Replace the following with your own k8s cluster contexts
contexts = {
    'us-west1-b': 'gke_yugabyte_us-west1-b_yugabytedb1',
    'us-central1-b': 'gke_yugabyte_us-central1-b_yugabytedb2',
    'us-east1-b': 'gke_yugabyte_us-east1-b_yugabytedb3',
}

# Replace the following with your own `zone`: `region` names
regions = {
    'us-west1-b': 'us-west1',
    'us-central1-b': 'us-central1',
    'us-east1-b': 'us-east1',
}

# Set the path to the directory where the generated yaml files will be stored
generated_files_dir = './generated'

try:
    os.mkdir(generated_files_dir)
except OSError:
    pass

# Create a load balancer for the DNS pods in each k8s cluster.
for zone, context in contexts.items():
    check_call(['kubectl', 'create', 'namespace', 'yb-demo-'+zone, '--context', context])
    check_call(['kubectl', 'apply', '-f', 'yb-dns-lb.yaml', '--context', context])

# Set up each load balancer to forward DNS requests for zone-scoped namespaces to the
# relevant cluster's DNS server, using the external IP of the internal load balancers
dns_ips = dict()
for zone, context in contexts.items():
    external_ip = ''
    while True:
        external_ip = check_output([
                'kubectl', 'get', 'svc', 'kube-dns-lb', '--namespace', 'kube-system', '--context',
                context, '--template', '{{range .status.loadBalancer.ingress}}{{.ip}}{{end}}'
            ]).decode('utf-8')
        if external_ip:
            break
        print('Waiting for DNS load balancer IP in %s...' % (zone))
        sleep(10)
    print('DNS endpoint for zone %s: %s' % (zone, external_ip))
    dns_ips[zone] = external_ip

# Update each cluster's DNS configuration with an appropriate configmap. Note
# that we have to ensure that the local cluster is not added to its own configmap
# since those requests do not go through the load balancer. Finally, we have to delete the
# existing DNS pods in order for the new configuration to take effect.
for zone, context in contexts.items():
    remote_dns_ips = dict()
    for z, ip in dns_ips.items():
        if z == zone:
            continue
        remote_dns_ips['yb-demo-'+z+'.svc.cluster.local'] = [ip]
    config_filename = '%s/dns-configmap-%s.yaml' % (generated_files_dir, zone)
    with open(config_filename, 'w') as f:
        f.write("""\
apiVersion: v1
kind: ConfigMap
metadata:
  name: kube-dns
  namespace: kube-system
data:
  stubDomains: |
    %s
""" % (json.dumps(remote_dns_ips)))
    check_call(['kubectl', 'apply', '-f', config_filename, '--namespace', 'kube-system',
                '--context', context])
    check_call(['kubectl', 'delete', 'pods', '-l', 'k8s-app=kube-dns', '--namespace', 'kube-system',
                '--context', context])
