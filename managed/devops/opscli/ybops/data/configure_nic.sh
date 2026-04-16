secondary_network=""
secondary_netmask=""
cloud=""
rtb_id=250 # Just free route table ID
rtb_name="secondary" # This is customer side of the routing table
mgmt_rtb_id=251
mgmt_rtb_name="mgmt" # This is management side of the routing table
tmp_dir="/tmp"

os_name=$([ -f /etc/os-release ] && grep -e "^ID=" /etc/os-release | cut -d = -f 2 | tr -d '"')
os_version=$([ -f /etc/os-release ] && grep -e "^VERSION_ID=" /etc/os-release | cut -d = -f 2 | \
tr -d '"' | cut -d . -f 1)

# AWS - Primary - ens5 :: Secondary interface can be - eth0 eth1 ens6
# GCP - Primary - eth0 or ens5 :: Secondary can be - eth1 ens6
# We will use secondary or the customer side to be the default route
# Adding two different route tables for mgmt and secondary(customer) sides, makes it
# scalable for future extensions and we can add more specific rules to each table
# rather than changing the default table we will continue to
# use the names secondary for customer side interface
# and primary for management interface, just to be compatible with previously installed nodes
# Though its important to know that secondary interface will become the default route
# whichever order it came up

fix_ifcfg() {
    # When using network manager dispatcher scripts, we do not want to mess with the DEFROUTE setup.
    # Setting it to no results in incorrect variables being set by the dispatcher.
    # E.g. $IP4_GATEWAY = 0.0.0.0/0 instead of the actual internet gateway IP.
    if [ "${use_network_manager_dispatcher}" = true ]; then
        return
    fi

    #eth0 - primary interface is ens5 and AWS setup eth0
    #eth1 - primary interface is eth0 and AWS configured secondary
    #ens6 - primary interface is ens5 and AWS messed secondary
    # We will check for all interfaces and make sure the DEFROUTE is not set
    # for any interface which ever order they come up(eth0 first, eth1 next or vice-versa)
    ifnames="eth0 eth1 ens5 ens6"
    for ifname in $ifnames; do
        if_config_file="/etc/sysconfig/network-scripts/ifcfg-$ifname"
        if [ -f "${if_config_file}" ]; then
            DEFROUTE=""
            source "${if_config_file}"
            echo "Current value of DEFROUTE for $ifname : ${DEFROUTE}"
            if [ "${DEFROUTE}" != "no" ]; then
                # It can be either yes or does not exisit, so we need to check
                if [ "${DEFROUTE}" = "yes" ]; then
                    echo "Replacing DEFROUTE for Interface:$ifname"
                    sed -i -e 's/DEFROUTE=.*/DEFROUTE=no/g' "${if_config_file}"
                else
                    echo "No entry exists for Interface:$ifname, creating"
                    echo -e "DEFROUTE=no" >> "${if_config_file}"
                fi
            fi
        else
            echo -e "BOOTPROTO=dhcp\nDEVICE=$ifname\nONBOOT=yes\nDEFROUTE=no\nTYPE=Ethernet\nUSERCTL=no
            " >"${if_config_file}"
        fi
    done
}

create_route_tables() {
    # Create the route table for the customer side.
    echo "Creating route tables"
    if grep -q "^${rtb_id}" /etc/iproute2/rt_tables; then
        echo "Route table with ID $rtb_id already exists"
    else
        echo -e "${rtb_id}\t$rtb_name" >>/etc/iproute2/rt_tables
        echo "Created route table ${rtb_name}"
    fi

    # Create the route table for the management side.
    echo "Creating route table ${mgmt_rtb_id}"
    if grep -q "^${mgmt_rtb_id}" /etc/iproute2/rt_tables; then
        echo "Route table with ID $mgmt_rtb_id already exists"
    else
        echo -e "${mgmt_rtb_id}\t$mgmt_rtb_name" >>/etc/iproute2/rt_tables
        echo "Created route table ${mgmt_rtb_name}"
    fi
}

configure_nics() {
    create_route_tables
    if [ "${use_network_manager_dispatcher}" = false ]; then
        # Disable Network Manager for any interface.
        echo "Disabling network manager"
        systemctl disable NetworkManager
        semanage permissive -a dhcpc_t
        up_hook_file="/etc/dhcp/dhclient-up-hooks"
    else
        # Keep NetworkManager enabled. Needed for RHEL 8+ (AlmaLinux 8, 9 OS).
        # We will use NetworkManager dispatcher scripts to activate the interfaces.
        # https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/
        # configuring_and_managing_networking/assembly_running-dhclient-exit-hooks-using-
        # networkmanager-a-dispatcher-script_configuring-and-managing-networking
        up_hook_file="/etc/NetworkManager/dispatcher.d/20-yb-configure-2-nics"
    fi

  cat - >"${up_hook_file}" <<EOF
#!/usr/bin/env bash
set -x

# Rebinding variables from configure_nic.sh script for context/clarity.
secondary_network="${secondary_network}"
secondary_netmask="${secondary_netmask}"

log(){
 echo "\$*" >> "\${log_file}"
}

# Function to add an ip policy rule.
# \$1 - the source network.
# \$2 - the source netmask.
# \$3 - the route table name to use for route lookup.
add_ip_rule() {
    if ip rule show | grep -q "from \${1}/\${2} lookup \${3}"; then
        log "Rule \${1}/\${2} -> \${3} already set"
    else
        ip rule add from \${1}/\${2} lookup \${3}
    fi
}

# Function to add a default route for a route table.
# \$1 - the route table to add the route to.
# \$2 - the gateway to route to.
# \$3 - the network interface.
add_default_route() {
    if ip route show table "\${1}" | grep "default via \${2} dev \${3}"; then
        log "Route default via \${2} dev \${3} table \${1} set"
    else
        ip route add default via "\${2}" dev "\${3}" table "\${1}"
    fi
}

# Function to configure dnat. Only required for GCP.
configure_dnat() {
    if [ "\$(systemctl is-enabled firewalld 2>/dev/null)" == 'enabled' ]; then
        log "DNAT for firewalld"
        echo >/etc/firewalld/direct.xml \\
            "<?xml version=\"1.0\" encoding=\"utf-8\"?><direct>" \\
            "<rule priority=\"0\" table=\"nat\" ipv=\"ipv4\" chain=\"PREROUTING\">" \\
            "-p tcp -i "\${interface}" -j DNAT --to-destination "\${new_ip_address}"" \\
            "</rule></direct>"
        [ \$(systemctl is-active firewalld) == 'active' ] && systemctl reload firewalld
    else
        log "DNAT for iptables"
        iptables -t nat -A PREROUTING \\
            -p tcp -i "\${interface}" -j DNAT --to-destination "\${new_ip_address}"
    fi
}

if [ "${use_network_manager_dispatcher}" = true ]; then
    # Only run network manager dispatcher scripts once the interface is up.
    if [ "\${NM_DISPATCHER_ACTION}" != "up" ]; then
        exit
    fi
    if [ -z "\${IP4_GATEWAY}" ] || [ -z "\${IP4_ROUTE_0}" ] || [ -z "\${DEVICE_IP_IFACE}" ]; then
        echo "IP4_GATEWAY=\${IP4_GATEWAY}"
        echo "IP4_ROUTE_0=\${IP4_ROUTE_0}"
        echo "DEVICE_IP_IFACE=\${DEVICE_IP_IFACE}"
        echo "The following variables have to be set: IP4_GATEWAY, IP4_ROUTE_0, DEVICE_IP_IFACE." \
             "Exiting."
        exit
    fi
    # Set script variables.
    interface="\${DEVICE_IP_IFACE}"
    new_ip_address="\${IP4_ADDRESS_0%%/*}"
    new_network_number="\${IP4_ROUTE_0%%/*}"
    new_routers="\${IP4_GATEWAY}"
    new_subnet_mask="\$(echo \"\${IP4_ROUTE_0}\" | grep -Po '(?<=/)[0-9]+')"

    # Check for /32 route (gateway route), if found look for second route entry
    if [ "\${new_subnet_mask}" = 32 ]; then
        # Check for a second route entry
        if [ -z "\${IP4_ROUTE_1}" ]; then
            echo "IP4_GATEWAY=\${IP4_GATEWAY}"
            echo "IP4_ROUTE_0=\${IP4_ROUTE_0}"
            echo "IP4_ROUTE_1=\${IP4_ROUTE_1}"
            echo "DEVICE_IP_IFACE=\${DEVICE_IP_IFACE}"
            echo "Found /32 route in IP4_ROUTE_0 and no second route in IP4_ROUTE_1. Exiting."
            exit
        fi
        new_network_number="\${IP4_ROUTE_1%%/*}"
        new_routers="\${IP4_ROUTE_0%%/*}"
        new_subnet_mask="\$(echo \"\${IP4_ROUTE_1}\" | grep -Po '(?<=/)[0-9]+')"
    fi
else
    if [ "$cloud" = "gcp" ]; then
        # Set script variables (already set for AWS).
        new_ip_address="\${new_ip_address:-\${IP4_ADDRESS_0%%/*}}"
        new_network_number="\${route_targets[1]}"
        new_routers="\${route_targets[0]}"
        new_subnet_mask="\$prefix"
    fi
fi
log_file="${tmp_dir}/dhclient-script-\${interface}-up-\$(date +%s)"
log "ENV: interface=\${interface}"
log "ENV: new_routers=\${new_routers}"
log "ENV: new_network_number=\${new_network_number}"
log "ENV: new_subnet_mask=\${new_subnet_mask}"
log "ENV: new_ip_address=\${new_ip_address}"

if [ "\$new_network_number" = "${secondary_network}" ]; then
    log "It's secondary CIDR (${secondary_network}), update rule and route!"
    add_ip_rule "${secondary_network}" "${secondary_netmask}" "${rtb_name}"
    add_default_route "${rtb_name}" "\${new_routers}" "\${interface}"

    # Add default route via the customer interface
    ip route del default
    ip route add default via "\${new_routers}"
else
    log "It's management CIDR, update rule and route!"
    add_ip_rule "\${new_network_number}" "\${new_subnet_mask}" "${mgmt_rtb_name}"
    add_default_route "${mgmt_rtb_name}" "\${new_routers}" "\${interface}"
    if [ '${cloud}' == 'gcp' ]; then
        log "Configure DNAT to allow GCP LB to send resps"
        configure_dnat
    fi
fi
log Done
EOF
    echo "Updated interface up hook script at ${up_hook_file}"
    if [ "${use_network_manager_dispatcher}" = true ]; then
        # File must only be accessible by the root user (rwx).
        chown root:root "${up_hook_file}"
        chmod 700 "${up_hook_file}"
    else
        chmod +x "${up_hook_file}"
    fi
    echo "Done"
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
    echo "$@" >&2
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
        --tmp_dir)
            tmp_dir="$2"
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

use_network_manager_dispatcher=false
[ "$cloud" = "aws" -o "$cloud" = "gcp" ] && [ "$os_version" = "8" -o "$os_version" = "9" ]  && \
  [ "$os_name" = "almalinux" ] && use_network_manager_dispatcher=true

fix_ifcfg
configure_nics
