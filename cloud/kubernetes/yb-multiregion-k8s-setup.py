#!/usr/bin/env python

import json
import os
import sys
import time
from subprocess import CalledProcessError, check_call, check_output
from pathlib import Path

# Configuration - replace these with your own values
CONTEXTS = {
    'us-west1-b': 'gke_yugabyte_us-west1-b_yugabytedb1',
    'us-central1-b': 'gke_yugabyte_us-central1-b_yugabytedb2',
    'us-east1-b': 'gke_yugabyte_us-east1-b_yugabytedb3',
}

REGIONS = {
    'us-west1-b': 'us-west1',
    'us-central1-b': 'us-central1',
    'us-east1-b': 'us-east1',
}

# Constants
GENERATED_FILES_DIR = Path('./generated')
DNS_LB_YAML = 'yb-dns-lb.yaml'
MAX_WAIT_SECONDS = 300  # 5 minutes max wait for LB IP
WAIT_INTERVAL = 10      # Check every 10 seconds

def validate_prerequisites():
    """Check if required tools are installed and accessible."""
    try:
        check_call(['kubectl', 'version', '--client'], stdout=sys.stderr, stderr=sys.stderr)
    except CalledProcessError as e:
        sys.exit(f"Error: kubectl is required but not available: {e}")
    except FileNotFoundError:
        sys.exit("Error: kubectl command not found. Please install kubectl.")

def create_generated_dir():
    """Create directory for generated files."""
    try:
        GENERATED_FILES_DIR.mkdir(exist_ok=True)
    except OSError as e:
        sys.exit(f"Error creating directory {GENERATED_FILES_DIR}: {e}")

def setup_load_balancers():
    """Create load balancers for DNS in each cluster."""
    for zone, context in CONTEXTS.items():
        namespace = f'yb-demo-{zone}'
        try:
            check_call(['kubectl', 'create', 'namespace', namespace, '--context', context])
            check_call(['kubectl', 'apply', '-f', DNS_LB_YAML, '--context', context])
            print(f"Successfully set up load balancer in {zone}")
        except CalledProcessError as e:
            sys.exit(f"Failed to set up load balancer in {zone}: {e}")

def get_load_balancer_ips():
    """Retrieve external IPs for DNS load balancers."""
    dns_ips = {}
    for zone, context in CONTEXTS.items():
        start_time = time.time()
        external_ip = ''
        
        print(f"Waiting for DNS load balancer IP in {zone}...")
        while time.time() - start_time < MAX_WAIT_SECONDS:
            try:
                external_ip = check_output([
                    'kubectl', 'get', 'svc', 'kube-dns-lb',
                    '--namespace', 'kube-system',
                    '--context', context,
                    '--template', '{{range .status.loadBalancer.ingress}}{{.ip}}{{end}}'
                ]).decode('utf-8').strip()
                
                if external_ip:
                    dns_ips[zone] = external_ip
                    print(f"DNS endpoint for zone {zone}: {external_ip}")
                    break
                
            except CalledProcessError as e:
                print(f"Error checking load balancer IP in {zone}: {e}")
            
            time.sleep(WAIT_INTERVAL)
        
        if zone not in dns_ips:
            sys.exit(f"Timed out waiting for DNS load balancer IP in {zone}")
    
    return dns_ips

def update_dns_configurations(dns_ips):
    """Update each cluster's DNS configuration with appropriate configmap."""
    for zone, context in CONTEXTS.items():
        remote_dns_ips = {
            f'yb-demo-{z}.svc.cluster.local': [ip]
            for z, ip in dns_ips.items() if z != zone
        }
        
        config_filename = GENERATED_FILES_DIR / f'dns-configmap-{zone}.yaml'
        
        try:
            with config_filename.open('w') as f:
                config_data = {
                    'apiVersion': 'v1',
                    'kind': 'ConfigMap',
                    'metadata': {
                        'name': 'kube-dns',
                        'namespace': 'kube-system'
                    },
                    'data': {
                        'stubDomains': json.dumps(remote_dns_ips)
                    }
                }
                json.dump(config_data, f, indent=2)
            
            # Apply the configmap
            check_call([
                'kubectl', 'apply', '-f', str(config_filename),
                '--namespace', 'kube-system', '--context', context
            ])
            
            # Restart DNS pods
            check_call([
                'kubectl', 'delete', 'pods', '-l', 'k8s-app=kube-dns',
                '--namespace', 'kube-system', '--context', context
            ])
            
            print(f"Successfully updated DNS configuration in {zone}")
            
        except (IOError, CalledProcessError) as e:
            sys.exit(f"Failed to update DNS configuration in {zone}: {e}")

def main():
    print("Validating prerequisites...")
    validate_prerequisites()
    
    print("Creating output directory...")
    create_generated_dir()
    
    print("Setting up load balancers...")
    setup_load_balancers()
    
    print("Retrieving load balancer IPs...")
    dns_ips = get_load_balancer_ips()
    
    print("Updating DNS configurations...")
    update_dns_configurations(dns_ips)
    
    print("\nGlobal DNS setup completed successfully!")

if __name__ == '__main__':
    main()
