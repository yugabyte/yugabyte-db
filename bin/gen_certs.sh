#!/bin/sh

root_certs_generation=false
node_certs_generation=false
root_certs_path=""
server_certs_path=""
temp_certs_path=""
hostname=""

show_help() {
  cat >&1 <<-EOT
gen_certs.sh is to be used to create SSL certificates for secure deployment of YugabyteDB.
---------------------------------------------------------------------------------------------------
Usage: ${0##*/} [<flags>]
Flags:

    --help, -h
        Show help.

    generate-ca
        Generate root certificates to be used for generation of each node's certificates

    generate-server-cert
        Generate node server certificates.

    --root-ca-path, --rcp, -rcp
        Path where root certs needs to be generated or where root certs are present (in case of \
node certs generation)

    --server-cert-path, --scp, -scp
        Path where the server certificates will be generated

    --hostname, --hn, -hn
        Value of node's commonName to be set in certificates. Needs to be same as the
        advertise address of the node
---------------------------------------------------------------------------------------------------
EOT
}

generate_root_certs() {
    mkdir -p $root_certs_path

    echo '# CA root configuration file
    [ ca ]
    default_ca = yugabyted_root_ca

    [ yugabyted_root_ca ]
    default_days = 730
    serial = '$root_certs_path'/serial.txt
    database = '$root_certs_path'/index.txt
    default_md = sha256
    policy = yugabyted_policy

    [ yugabyted_policy ]
    organizationName = supplied
    commonName = supplied

    [req]
    prompt=no
    distinguished_name = YugabyteDB
    x509_extensions = YugabyteDB_extensions

    [ YugabyteDB ]
    organizationName = Yugabyte
    commonName = Root CA for YugabyteDB

    [ YugabyteDB_extensions ]
    keyUsage = critical,digitalSignature,nonRepudiation,keyEncipherment,keyCertSign
    basicConstraints = critical,CA:true,pathlen:1' > $root_certs_path/ca.conf

    touch $root_certs_path/index.txt
    echo '01' > $root_certs_path/serial.txt
    openssl genrsa -out $root_certs_path/ca.key >&1
    chmod 400 $root_certs_path/ca.key
    openssl req -new -x509 -config $root_certs_path/ca.conf \
                -key $root_certs_path/ca.key \
                -out $root_certs_path/ca.crt
}

generate_node_certs() {
    mkdir -p $temp_certs_path

    cp $root_certs_path/ca.crt $temp_certs_path/

    echo '# Example node configuration file
    [ req ]
    prompt=no
    distinguished_name = YugabyteDB_Node

    [ YugabyteDB_Node ]
    organizationName = Yugabyte
    commonName = '$hostname > $temp_certs_path/node.conf


    openssl genrsa -out $temp_certs_path/node.$hostname.key
    chmod 400 $temp_certs_path/node.$hostname.key

    openssl req -new -config $temp_certs_path/node.conf \
                -key $temp_certs_path/node.$hostname.key \
                -out $temp_certs_path/node.csr


    openssl ca -config $root_certs_path/ca.conf \
                -keyfile $root_certs_path/ca.key \
                -cert $root_certs_path/ca.crt \
                -policy yugabyted_policy \
                -out $temp_certs_path/node.$hostname.crt \
                -outdir $temp_certs_path \
                -in $temp_certs_path/node.csr \
                -days 730 \
                -batch

    cp $temp_certs_path/ca.crt \
        $temp_certs_path/node.$hostname.key \
        $temp_certs_path/node.$hostname.crt \
        $server_certs_path

    rm -rf $temp_certs_path
}

while [[ $# -gt 0 ]]; do
    case ${1//_/-} in
    -h|--help)
      show_help >&1
      exit 1
    ;;
    generate-ca)
        root_certs_generation=true
    ;;
    --root-ca-path|--rcp|-rcp)
        root_certs_path=$2
        shift
    ;;
    generate-server-cert)
        node_certs_generation=true
    ;;
    --server-certs-path|--scp|-scp)
        server_certs_path=$2
        shift
    ;;
    --hostname|--hn|-hn)
        hostname=$2
        shift
    ;;
    esac
    shift
done

if "$root_certs_generation"; then
    generate_root_certs
fi

if "$node_certs_generation"; then
    temp_certs_path=$server_certs_path"/temp"
    generate_node_certs
fi
