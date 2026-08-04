#!/usr/bin/env python3

import argparse
import os
import socket
import subprocess
from datetime import timedelta
from typing import List, Optional, Tuple

default_yb_admin_path = "/home/yugabyte/master/bin/yb-admin"
default_master_port = 7100
default_tserver_port = 9100


def verify_connectivity(ip: str, port: int, timeout: timedelta) -> bool:
    """
    Check if the given IP and port are accessible.

    Parameters:
    - ip (str): The IP address to check.
    - port (int): The port to check.
    - timeout (timedelta): Timeout duration for the connection attempt.

    Returns:
    - bool: True if the IP and port are accessible, False otherwise.
    """
    try:
        timeout_seconds = timeout.total_seconds()
        if timeout_seconds <= 0:
            print(
                "WARN: Timeout duration is less than or equal to 0 seconds. Using 1 "
                "second instead."
            )
            timeout_seconds = 1
        with socket.create_connection((ip, port), timeout_seconds):
            return True
    except (socket.timeout, socket.error):
        return False


def get_master_addresses_with_failed_connectivity(
    master_addresses: List[Tuple[str, Optional[int]]], timeout: timedelta
) -> List[Tuple[str, int]]:
    """
    Get the YB Master server addresses that failed connectivity check.

    Parameters:
    - master_addresses (List[Tuple[str, Optional[int]]]): List of YB Master server addresses
    as (ip, port) tuples.
    - timeout (timedelta): Timeout duration for the connection attempt.

    Returns:
    - List[Tuple[str, int]]: List of addresses that failed connectivity.
    """
    failed_addresses = []
    for ip, port in master_addresses:
        if port is None:
            port = default_master_port
        if not verify_connectivity(ip, port, timeout):
            failed_addresses.append((ip, port))
    return failed_addresses


def format_master_addresses(master_addresses: List[Tuple[str, Optional[int]]]) -> str:
    formatted_addresses = []
    for ip, port in master_addresses:
        if port is None:
            port = default_master_port
        formatted_addresses.append(f"{ip}:{port}")
    return ",".join(formatted_addresses)


def run_yb_admin_command(
    yb_admin_path: str,
    subcommand: List[str],
    master_addresses: List[Tuple[str, Optional[int]]],
    root_ca_dir: Optional[str],
    timeout: timedelta,
) -> str:
    """
    Run a yb-admin command and return the output.

    Parameters:
    - subcommand (List[str]): List of command arguments. It starts with the yb-admin subcommand.
    - master_addresses (List[Tuple[str, Optional[int]]]): List of YB Master server addresses
    as (ip, port) tuples.
    - root_ca_dir (Optional[str]): Path to the root CA certificate directory.
    - timeout (timedelta): Timeout duration for the RPC connection attempt.

    Returns:
    - str: The output of the command.
    """
    formatted_addresses = format_master_addresses(master_addresses)
    command = [
        yb_admin_path,
        "-master_addresses",
        formatted_addresses,
        "-timeout_ms",
        str(int(timeout.total_seconds() * 1000)),
    ]
    if root_ca_dir:
        command.extend(["-certs_dir_name", root_ca_dir])
    command.extend(subcommand)
    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )
        if result.returncode != 0:
            raise Exception(
                f"Command {command} failed with error code: "
                f"{result.returncode}\nSTDOUT: {result.stdout}\nSTDERR: {result.stderr}"
            )
        return result.stdout
    except Exception as e:
        print(f"Command {command} hit exception: {str(e)}")
        raise


def list_all_masters(
    yb_admin_path: str,
    master_addresses: List[Tuple[str, Optional[int]]],
    root_ca_dir: Optional[str],
    timeout: timedelta,
) -> List[Tuple[str, int]]:
    yb_admin_output = run_yb_admin_command(
        yb_admin_path, ["list_all_masters"], master_addresses, root_ca_dir, timeout
    )
    masters = []
    for line in yb_admin_output.splitlines():
        if line.strip() and "UUID" not in line:  # Ignore headers and empty lines
            parts = line.split()
            rpc_host_port = parts[1]
            broadcast_host_port = parts[4] if len(parts) > 4 else "N/A"
            if broadcast_host_port != "N/A":
                ip, port = broadcast_host_port.split(":")
            else:
                ip, port = rpc_host_port.split(":")
            masters.append((ip, int(port)))
    return masters


def list_all_tservers(
    yb_admin_path: str,
    master_addresses: List[Tuple[str, Optional[int]]],
    root_ca_dir: Optional[str],
    timeout: timedelta,
) -> List[Tuple[str, int]]:
    yb_admin_output = run_yb_admin_command(
        yb_admin_path,
        ["list_all_tablet_servers"],
        master_addresses,
        root_ca_dir,
        timeout,
    )
    tservers = []
    for line in yb_admin_output.splitlines():
        if (
            line.strip() and "UUID" not in line and "Tablet Server" not in line
        ):  # Ignore headers and empty lines
            parts = line.split()
            rpc_host_port = parts[1]
            broadcast_host_port = parts[14] if len(parts) > 14 else "N/A"
            if broadcast_host_port != "N/A":
                ip, port = broadcast_host_port.split(":")
            else:
                ip, port = rpc_host_port.split(":")
            tservers.append((ip, int(port)))
    return tservers


def format_tserver_address(tserver_address: Tuple[str, Optional[int]]) -> str:
    ip, port = tserver_address
    if port is None:
        port = default_tserver_port
    return f"{ip}:{port}"


def run_yb_ts_cli_command(
    yb_ts_cli_path: str,
    subcommand: List[str],
    server_address: Tuple[str, Optional[int]],
    root_ca_dir: Optional[str],
    timeout: timedelta,
) -> str:
    """
    Run a yb-ts-cli command and return the output.

    Parameters:
    - yb_ts_cli_path (str): Path to the yb-ts-cli binary.
    - subcommand (List[str]): List of command arguments. It starts with the yb-ts-cli subcommand.
    - server_address (Tuple[str, Optional[int]]): The address of the tserver to run against.
    - root_ca_dir (Optional[str]): Path to the root CA certificate directory.
    - timeout (timedelta): Timeout duration for the RPC connection attempt.

    Returns:
    - str: The output of the command.
    """
    formatted_address = format_tserver_address(server_address)
    command = [
        yb_ts_cli_path,
        f"-server_address={formatted_address}",
        "-timeout_ms",
        str(int(timeout.total_seconds() * 1000)),
    ]
    if root_ca_dir:
        command.extend(["--certs_dir_name", root_ca_dir])
    command.extend(subcommand)
    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )
        if result.returncode != 0:
            raise Exception(
                f"Command {command} failed with error code: "
                f"{result.returncode}\nSTDOUT: {result.stdout}\nSTDERR: {result.stderr}"
            )
        return result.stdout
    except Exception as e:
        print(f"Command {command} hit exception: {str(e)}")
        raise


def is_tserver_ready(
    yb_ts_cli_path: str,
    server_address: Tuple[str, Optional[int]],
    root_ca_dir: Optional[str],
    timeout: timedelta,
) -> bool:
    """
    Check if the tablet server is ready.

    Parameters:
    - yb_ts_cli_path (str): Path to the yb-ts-cli binary.
    - server_address (Tuple[str, Optional[int]]): The address of the tserver to check.
    - root_ca_dir (Optional[str]): Path to the root CA certificate directory.
    - timeout (timedelta): Timeout duration for the RPC connection attempt.

    Returns:
    - bool: True if the server is ready, False otherwise.
    """
    try:
        output = run_yb_ts_cli_command(
            yb_ts_cli_path, ["is_server_ready"], server_address, root_ca_dir, timeout
        )
        return "Tablet server is ready" in output
    except Exception as e:
        print(f"Error checking if server {server_address} is ready: {str(e)}")
        return False


def is_tserver_accessible(
    yb_ts_cli_path: str,
    server_address: Tuple[str, Optional[int]],
    root_ca_dir: Optional[str],
    timeout: timedelta,
) -> bool:
    """
    Check if the tablet server is accessible.

    Parameters:
    - yb_ts_cli_path (str): Path to the yb-ts-cli binary.
    - server_address (Tuple[str, Optional[int]]): The address of the tserver to check.
    - root_ca_dir (Optional[str]): Path to the root CA certificate directory.
    - timeout (timedelta): Timeout duration for the RPC connection attempt.

    Returns:
    - bool: True if the server is accessible, False otherwise.
    """
    try:
        run_yb_ts_cli_command(
            yb_ts_cli_path, ["status"], server_address, root_ca_dir, timeout
        )
        return True
    except Exception:
        return False


def get_yb_ts_cli_path(yb_admin_path: str) -> str:
    """
    Find the yb-ts-cli binary.

    Parameters:
    - yb_admin_path (str): Path to the yb-admin binary.

    Returns:
    - str: Path to the yb-ts-cli binary.
    """
    yb_admin_parent_directory = os.path.dirname(yb_admin_path)
    for root, dirnames, filenames in os.walk(yb_admin_parent_directory):
        for filename in filenames:
            if filename == "yb-ts-cli":
                return os.path.join(root, filename)
    raise Exception("yb-ts-cli binary not found")


def ensure_yb_admin_exists(yb_admin_path: str, timeout: timedelta):
    """
    Ensure that the yb-admin binary exists.

    Parameters:
    - yb_admin_path (str): Path to the yb-admin binary.
    """
    try:
        run_yb_admin_command(
            yb_admin_path, ["--version"], [("localhost", None)], None, timeout
        )
    except Exception as e:
        raise Exception(f"yb-admin binary not found at path: {yb_admin_path}: {e}")


def ensure_root_ca_dir_contains_ca_crt_file(root_ca_dir: str):
    """
    Ensure that the root CA directory contains the 'ca.crt' file.

    Parameters:
    - root_ca_dir (str): Path to the root CA certificate directory.
    """
    if not os.path.exists(root_ca_dir):
        raise Exception(f"Root CA directory does not exist: {root_ca_dir}")
    if not os.path.exists(os.path.join(root_ca_dir, "ca.crt")):
        raise Exception(
            f"Root CA directory does not contain 'ca.crt' file: {root_ca_dir}"
        )


def ensure_connectivity_to_all_servers(
    master_addresses: List[Tuple[str, Optional[int]]],
    root_ca_dir: Optional[str],
    timeout: timedelta,
    yb_admin_path: str,
):
    """
    Verify connectivity to all YB Master and Tablet servers.

    Parameters:
    - master_addresses (List[Tuple[str, Optional[int]]]): List of YB Master server addresses
    as (ip, port) tuples.
    - root_ca_dir (Optional[str]): Path to the root CA certificate directory.
    - timeout (timedelta): Timeout duration for the connection attempt.
    - yb_admin_path (str): Path to the yb-admin binary.
    """
    ensure_yb_admin_exists(yb_admin_path, timeout)
    if root_ca_dir:
        ensure_root_ca_dir_contains_ca_crt_file(root_ca_dir)

    masters = list_all_masters(yb_admin_path, master_addresses, root_ca_dir, timeout)
    for ip, port in master_addresses:
        ip_port = (ip, port if port else default_master_port)
        if ip_port not in masters:
            print(
                f"WARN: The ip:port {ip_port} was passed in as a master address but was "
                f"not found in the list of master servers received from the master leader: "
                f"{masters}. Master connectivity check to that ip will be ignored."
            )
    master_addresses_with_failed_connectivity = (
        get_master_addresses_with_failed_connectivity(masters, timeout)
    )
    if master_addresses_with_failed_connectivity:
        raise Exception(
            f"Not all master servers are accessible. Failed connection to "
            f"{master_addresses_with_failed_connectivity}"
        )

    tservers = list_all_tservers(yb_admin_path, master_addresses, root_ca_dir, timeout)
    yb_ts_cli_path = None
    try:
        yb_ts_cli_path = get_yb_ts_cli_path(yb_admin_path)
    except Exception as e:
        print(
            f"WARN: yb-ts-cli binary not found, will use python socket to verify connectivity: "
            f"{e}"
        )
        pass
    tserver_addresses_with_failed_connectivity = []
    for tserver in tservers:
        if yb_ts_cli_path:
            if not is_tserver_accessible(yb_ts_cli_path, tserver, root_ca_dir, timeout):
                tserver_addresses_with_failed_connectivity.append(tserver)
        else:
            if not verify_connectivity(tserver[0], tserver[1], timeout):
                tserver_addresses_with_failed_connectivity.append(tserver)
    if tserver_addresses_with_failed_connectivity:
        raise Exception(
            f"Not all tablet servers are accessible. Failed connection to "
            f"{tserver_addresses_with_failed_connectivity}"
        )


def execute_ensure_connectivity_to_all_servers(
    master_addresses: str, root_ca_dir: Optional[str], timeout: int, yb_admin_path: str
):
    """
    Verify connectivity to all YB Master and Tablet servers.

    Parameters:
    - master_addresses (str): Comma-separated list of YB Master server addresses in
    the format ip:port.
    - root_ca_dir (Optional[str]): Path to the root CA certificate directory.
    - timeout (int): Timeout duration for the connection attempt in seconds.
    - yb_admin_path (str): Path to the yb-admin binary.
    """
    master_addresses_list = [
        (addr.split(":")[0], int(addr.split(":")[1]) if ":" in addr else None)
        for addr in master_addresses.split(",")
    ]
    ensure_connectivity_to_all_servers(
        master_addresses_list, root_ca_dir, timedelta(seconds=timeout), yb_admin_path
    )
    print("All YB Master and Tablet servers are accessible.")


def main():
    parser = argparse.ArgumentParser(
        description="Verify connectivity to all YB Master and Tablet servers from this node. "
        "It uses yb-admin to connect to the master leader whose address is passed "
        "in and automatically finds out all the master and tserver IP addresses.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "master_addresses",
        type=str,
        help="Comma-separated list of YB Master server addresses in the format ip:port "
        "that contains the master leader.",
    )
    parser.add_argument(
        "--root_ca_dir",
        type=str,
        help="Path to the root CA certificate directory containing 'ca.crt' file.",
        default=None,
    )
    parser.add_argument(
        "--timeout",
        type=int,
        help="Timeout duration for the connection attempt in seconds.",
        default=10,
    )
    parser.add_argument(
        "--yb_admin_path",
        type=str,
        help="Path to the yb-admin binary.",
        default=default_yb_admin_path,
    )
    args = parser.parse_args()

    execute_ensure_connectivity_to_all_servers(
        master_addresses=args.master_addresses,
        root_ca_dir=args.root_ca_dir,
        timeout=args.timeout,
        yb_admin_path=args.yb_admin_path,
    )


if __name__ == "__main__":
    main()
