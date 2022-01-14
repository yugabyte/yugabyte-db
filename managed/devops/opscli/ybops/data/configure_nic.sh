secondary_network=""
secondary_netmask=""
cloud=""
rtb_id=250 # Just free route table ID
rtb_name="secondary" # This is customer side of the routing table
mgmt_rtb_id=251
mgmt_rtb_name="mgmt" # This is management side of the routing table

# AWS - Primary - ens5 :: Secondary interface can be - eth0 eth1 ens6
# GCP - Primary - eth0 or ens5 :: Secondary can be - eth1 ens6
# We will use secondary or the customer side to be the default route
# Adding two different route tables for mgmt and secondary(customer) sides, makes it
# scalable for future extensions and we can add more specific rules to each table
# rather than changing default table

fix_ifcfg() {
  #eth0 - primary interface is ens5 and AWS setup eth0
  #eth1 - primary interface is eth0 and AWS configured secondary
  #ens6 - primary interface is ens5 and AWS messed secondary
  ifnames="eth0 eth1 ens6"
  for ifname in $ifnames; do
    if_config_file="/etc/sysconfig/network-scripts/ifcfg-$ifname"
    [ -f "${if_config_file}" ] || echo "BOOTPROTO=dhcp
  DEVICE=$ifname
  ONBOOT=yes
  DEFROUTE=no
  TYPE=Ethernet
  USERCTL=no
  " >"${if_config_file}"
  done
}

configure_nics() {
  # Create Table for customer side
  echo "Create route table ${rtb_id}"
  egrep "^${rtb_id}" /etc/iproute2/rt_tables && {
    echo "RTb ID $rtb_id exists, change to $(($rtb_id + 1))"
    exit 1
  }
  echo -e "${rtb_id}\t$rtb_name" >>/etc/iproute2/rt_tables
  # Create Table for Mgmt side
  echo "Create route table ${mgmt_rtb_id}"
    egrep "^${mgmt_rtb_id}" /etc/iproute2/rt_tables && {
      echo "RTb ID mgmt_rtb_id exists, change to $((mgmt_rtb_id + 1))"
      exit 1
    }
    echo -e "${mgmt_rtb_id}\t$mgmt_rtb_name" >>/etc/iproute2/rt_tables

  cat - >/etc/dhcp/dhclient-up-hooks <<EOF
#!/usr/bin/env bash
set -x
log_file="/tmp/dhclient-script-up-hook-\${interface}-\$(date)-\$(uuidgen)"
log(){
 echo "\$*" >> "\${log_file}"
}
# Parameters
secondary_network="${secondary_network}"
secondary_netmask="${secondary_netmask}"

if [[ "$cloud" == "gcp" ]]; then
  # Re-bind variable to comply with AWS
  new_network_number="\$target"
  new_routers="\$gateway"
fi

log "Configure for \$new_network_number"

if [ "\$new_network_number" == "\${secondary_network}" ]; then
 log "It's secondary CIDR (\${secondary_network}), update rule and route!"
 ip rule show | grep "from \${secondary_network}/\${secondary_netmask} table $rtb_name" && {
   log "Rule already set"
 } || {
   ip rule add from \${secondary_network}/\${secondary_netmask} table $rtb_name
 }
 ip route show | grep "default table $rtb_name via \$new_routers dev \$interface" && {
   log "Route already set"
 } || {
   ip route add default table $rtb_name via \$new_routers dev \$interface
 }
 #Add default route via the customer interface
 ip route del default
 ip route add default via \$new_routers
else
 log "It's management CIDR !"
 ip rule show | grep "from \${$new_network_number}/\${new_subnet_mask} table $mgmt_rtb_name" && {
   log "Rule already set"
 } || {
   ip rule add from \${$new_network_number}/\${new_subnet_mask} table $mgmt_rtb_name
 }
 ip route show | grep "default table $mgmt_rtb_name via \$new_routers dev \$interface" && {
   log "Route already set"
 } || {
   ip route add default table $mgmt_rtb_name via \$new_routers dev \$interface
 }
fi
log Done
EOF
  chmod +x /etc/dhcp/dhclient-up-hooks

}

show_usage() {
  cat <<-EOT
Usage: ${0##*/} --subnet_network NETWORK --subnet_netmask MASK --cloud CLOUD

Options:
  --cloud CLOUD
    The deployment cloud [Must be aws or gcp]
  --subnet_network NETWORK
    The network of the subnet to configure
  --subnet_netmask MASK
    The network mask of the subnet
  -h, --help
    Show usage.
EOT
}

err_msg() {
  echo $@ >&2
}

if [[ ! $# -gt 0 ]]; then
  show_usage
  exit 1
fi

while [[ $# -gt 0 ]]; do
  case $1 in
  --cloud)
    options="aws gcp"
    if [[ ! $options =~ (^|[[:space:]])"$2"($|[[:space:]]) ]]; then
      err_msg "Invalid option: $2. Must be one of ['aws', 'gcp'].\n"
      show_usage >&2
      exit 1
    fi
    cloud="$2"
    shift
    ;;
  --subnet_network)
    secondary_network="$2"
    shift
    ;;
  --subnet_netmask)
    secondary_netmask="$2"
    shift
    ;;
  -h | --help)
    show_usage >&2
    exit 1
    ;;
  *)
    err_msg "Invalid option: $1\n"
    show_usage >&2
    exit 1
    ;;
  esac
  shift
done

if [[ "$cloud" == "aws" ]]; then
  fix_ifcfg
fi
configure_nics
