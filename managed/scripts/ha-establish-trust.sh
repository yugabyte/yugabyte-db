#!/bin/bash
set -euo pipefail

function print_usage {
  echo "Usage: ./ha-establish-trust <target_ha_node_url> <admin_api_token_for_target_ha_node> <host_1> <host_2> ... <host_n>"
  echo "Examples:"
  echo "In a three node HA cluster we want to setup so that https://portal.region1.com should trust ca certs for 1.2.3.4 and portal.region2.com"
  echo "$ ./ha-establish-trust https://portal.region1.com 3d32b451-eb02-4c22-93ea-eef8d7048218 1.2.3.4 portal.region2.com"
  echo "Note that in an HA mesh of 3 clusters above like you will have to run this command three times such that each host trusts the other two"
  exit 1
}

if [ $# -lt 3 ]; then
  echo "Insufficient arguments"
  print_usage
fi

if [ $# -gt 3 ]; then
  echo "Unsupported. So far can only work for HA cluster of 2 nodes"
  print_usage
fi

if [ "$1" == "--help" ]; then
  print_usage
fi

./set-runtime-config.sh $1 $2 yb.ha.ws "{ssl.trustManager {
 stores += {
    type = PEM
    data = \"\"\"$(keytool -printcert -sslserver $3 -rfc | tr -d '\r' | tac | awk '/-----END CERTIFICATE-----/{f=1} f; /-----BEGIN CERTIFICATE-----/{exit}' | tac)\"\"\"
    }
  }
}"
