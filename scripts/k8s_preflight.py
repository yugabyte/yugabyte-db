#!/usr/bin/env python

from __future__ import print_function
import argparse
import signal
import socket
import sys
import time
import resource

# Skipping following resources to verify because
# python resource module does not have related attriutes.
# Some of them are available in Python v3 but not all.
# pipe size, threshold - 8
# virtual memory, threshold - unlimited (RLIMIT_VMEM)
# file locks, threshold - unlimited
# scheduling priority, threshold - 0 (RLIMIT_NICE)
# pending signals, threshold - 119934, (RLIMIT_SIGPENDING)
# POSIX message queues, threshold - 819200 (RLIMIT_MSGQUEUE)
# real-time priority, threshold - 0 (RLIMIT_RTPRIO)
# As of now, necessary ulimit resources for YB are in verification RLIMIT_NOFILE and RLIMIT_NPROC
# Ref - https://docs.yugabyte.com/preview/deploy/manual-deployment/system-config/#ulimits

REQUIRED_RESOURCES = {
    "RLIMIT_NOFILE": (1048576, "open files"),
    "RLIMIT_NPROC": (12000, "max user processes"),
}

OPTIONAL_RESOURCES = {
    "RLIMIT_DATA": (-1, "data seg size"),
    "RLIMIT_MEMLOCK": (65536, "max locked memory"),  # 64 kbytes, 65536 bytes
    "RLIMIT_AS": (-1, "max memory size"),
    "RLIMIT_STACK": (8388608, "stack size"),  # 8192 kbytes, 8388608 bytes
    "RLIMIT_CORE": (-1, "core file size"),
    "RLIMIT_FSIZE": (-1, "file size"),
    "RLIMIT_CPU": (-1, "cpu time"),
}


def wait_for_dns_resolve(addr):
    print("DNS addr resolve: " + addr)
    while True:
        # Try to perform DNS lookup once every second until the query succeeds.
        while True:
            try:
                socket.getaddrinfo(addr, None)
                break
            except socket.error as e:
                print("DNS addr resolve failed. Error: " + str(e) + ". Retrying.")
                time.sleep(1)
        # After first resolve, ensure DNS lookups succeed multiple times in a
        # row to achieve some level of confidence that every DNS server is
        # consistent and can resolve the address.
        for _ in range(10):
            try:
                socket.getaddrinfo(addr, None)
            except socket.error as e:
                print("DNS addr resolve failed. Error: " + str(e) + ". Retrying.")
                time.sleep(1)
                break
        else:
            print("DNS addr resolve success.")
            return


def wait_for_bind(addr, port):
    # Get address info list.
    addr_infos = socket.getaddrinfo(addr, port)

    for (addr_family, addr_type, _, _, sock_addr) in addr_infos:
        # Filter so we only attempt to bind IPV4/6 TCP addresses.
        if addr_family != socket.AF_INET and addr_family != socket.AF_INET6:
            continue
        if addr_type != socket.SOCK_STREAM:
            continue

        # Print address info.
        addr_family_str = "ipv4" if (addr_family == socket.AF_INET) else "ipv6"
        print("Bind %s: %s port %d" % (addr_family_str, sock_addr[0], port))

        # Attempt to bind socket.
        sock = socket.socket(addr_family, addr_type)
        while True:
            try:
                sock.bind(sock_addr)
                sock.close()
                print("Bind success.")
                break
            except socket.error as e:
                print("Bind failed. Error: " + str(e) + ". Retrying.")
                time.sleep(1)


def parse_addr(addr):
    num_colons = addr.count(":")
    if num_colons == 0:
        # If no colons exist, return address.
        # Examples:
        #   1.2.3.4 -> 1.2.3.4
        #   foo.com -> foo.com
        return addr
    if num_colons == 1:
        # If one colon exists, return address without port.
        # Examples:
        #   1.2.3.4:567 -> 1.2.3.4
        #   foo.com:123 -> foo.com
        return addr.split(":")[0]
    else:
        # If >1 colon exists, parse ipv6 address.
        left_pos = addr.find("[")
        right_pos = addr.find("]")
        if left_pos != -1 and right_pos != -1:
            # If left and right brackets exist, return inner portion.
            # Examples:
            #   [::]:123 -> ::
            #   [::] -> ::
            return addr[left_pos+1:right_pos]
        else:
            # If no left and right brackets exist, return ipv6 address
            # Examples:
            #   1111:2222:3333:4444:5555:6666:7777:8888 -> 1111:2222:3333:4444:5555:6666:7777:8888
            #   :: -> ::
            return addr


def verify_ulimit(ulimit, alert_msgs):
    alert_template = "{} | Expected Value : {} | Current Value : {}"

    for ulimit_resource, (threshold, resource_name) in ulimit.items():
        soft_limit, _ = resource.getrlimit(getattr(resource, ulimit_resource))

        # Cover all the cases where threshold equals to -1 and
        # soft limit set to some value. -1 means unlimited.
        if threshold == -1:
            if soft_limit != threshold:
                alert_msgs.append(
                    alert_template.format(
                        resource_name, str(threshold), str(soft_limit)
                    )
                )
        # Cover all the cases where threshold set to some value and
        # soft limit set to some other lower value and soft limit
        # should be some arbitrary value not -1.
        # Ex- max user processes - threshold 12000 and few cloud
        # provider set it to -1.
        elif soft_limit < threshold and soft_limit != -1:
            alert_msgs.append(
                alert_template.format(resource_name, str(threshold), str(soft_limit))
            )


if __name__ == "__main__":
    # Parse CLI args.
    parser = argparse.ArgumentParser()
    subparser = parser.add_subparsers(dest="preflight_check")

    parser.add_argument("--timeout", type=int, default=300)  # 300 seconds = 5 minutes

    # Common Subparser
    # 1. Ulimit preflight
    subparser_common = subparser.add_parser(
        "all", help="All preflight checks except dnscheck"
    )
    subparser_common.add_argument(
        "--skip_ulimit", action="store_const", const=True, help="Skip Ulimit"
    )

    # DNS Preflight
    subparser_dnscheck = subparser.add_parser("dnscheck", help="DNS Preflight")
    subparser_dnscheck.add_argument(
        "--addr", action="append", required=True, help="Address to check"
    )
    group = subparser_dnscheck.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "--port", type=int, action="append", help="Port used to check bind"
    )
    group.add_argument(
        "--skip_bind", action="store_const", const=True, help="Skip port binding"
    )

    args = parser.parse_args()

    # Setup script timeout.
    signal.signal(signal.SIGALRM, lambda *_: sys.exit("Error: Timeout"))
    signal.alarm(args.timeout)

    if args.preflight_check == "dnscheck":
        # Perform dns preflight checks.
        for addr in args.addr:
            addr = parse_addr(addr)
            wait_for_dns_resolve(addr)
            if args.skip_bind:
                continue
            for port in args.port:
                wait_for_bind(addr, port)
    elif args.preflight_check == "all":
        # Perform ulimit verification
        if not args.skip_ulimit:
            ALERT_MESSAGE = str("{} ulimit values too low, see below. In kubernetes"
                                + "set the helm override 'preflight.skipUlimit: false'"
                                + "to override this check")
            mandatory_missed_ulimits = [ALERT_MESSAGE.format("Required")]
            optional_missed_ulimits = [ALERT_MESSAGE.format("Optional")]

            for ulimit_list, alert_messages, exit_action in (
                OPTIONAL_RESOURCES,
                optional_missed_ulimits,
                False,
            ), (REQUIRED_RESOURCES, mandatory_missed_ulimits, True):
                verify_ulimit(ulimit_list, alert_messages)

                if len(alert_messages) > 1:
                    print(str("\n".join(alert_messages)), file=sys.stderr)

                    if exit_action:
                        sys.exit(1)
