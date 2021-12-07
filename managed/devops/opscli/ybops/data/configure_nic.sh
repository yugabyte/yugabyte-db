secondary_network=""
secondary_netmask=""
cloud=""
rtb_id=250 # Just free route table ID
rtb_name="secondary"

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

configure_second_nic() {
  echo "Create route table ${rtb_id}"
  egrep "^${rtb_id}" /etc/iproute2/rt_tables && {
    echo "RTb ID $rtb_id exists, change to $(($rtb_id + 1))"
    exit 1
  }
  echo -e "${rtb_id}\t$rtb_name" >>/etc/iproute2/rt_tables
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
[ "\$new_network_number" == "\${secondary_network}" ] && {
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
}
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
configure_second_nic
