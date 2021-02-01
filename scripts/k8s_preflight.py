#!/usr/bin/env python

import argparse
import signal
import socket
import sys
import time


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
    num_colons = addr.count(':')
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
        return addr.split(':')[0]
    else:
        # If >1 colon exists, parse ipv6 address.
        left_pos = addr.find('[')
        right_pos = addr.find(']')
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


if __name__ == "__main__":
    # Parse CLI args.
    parser = argparse.ArgumentParser()
    parser.add_argument("--addr", action="append", required=True)
    parser.add_argument("--timeout", type=int, default=300)  # 300 seconds = 5 minutes
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--port", type=int, action="append")
    group.add_argument("--skip_bind", action="store_const", const=True)
    args = parser.parse_args()

    # Setup script timeout.
    signal.signal(signal.SIGALRM, lambda *_: sys.exit("Error: Timeout"))
    signal.alarm(args.timeout)

    # Perform preflight checks.
    for addr in args.addr:
        addr = parse_addr(addr)
        wait_for_dns_resolve(addr)
        if args.skip_bind:
            continue
        for port in args.port:
            wait_for_bind(addr, port)
