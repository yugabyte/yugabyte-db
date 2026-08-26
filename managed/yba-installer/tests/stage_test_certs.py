#!/usr/bin/env python3
#
# Copyright (c) YugabyteDB, Inc.
#
"""Create a test https server cert/key pair and copy it to a test machine.

Produces the certs needed to exercise the server-cert-hostname preflight check by
hand. Point yba-ctl.yml at the copied files, set host, and run 'yba-ctl preflight'
(or install/reconfigure) to see what the check makes of them.

Each mode puts the name in exactly one place, so a run tests one thing:

  # SAN covers the target -- check should pass
  ./tests/stage_test_certs.py 10.150.7.24

  # name only in the Common Name -- check should pass
  ./tests/stage_test_certs.py 10.150.7.24 --cn

  # no SAN and no Common Name -- check should pass, logging a warning
  ./tests/stage_test_certs.py 10.150.7.24 --blank

  # negative tests: the cert is issued for some other address
  ./tests/stage_test_certs.py 10.150.7.24 --name 10.0.0.99
  ./tests/stage_test_certs.py 10.150.7.24 --name 10.0.0.99 --cn

The certs are self signed, which is all the check looks at -- it parses the leaf and
matches names, and does not verify the chain.
"""

import argparse
import ipaddress
import shutil
import sys
from pathlib import Path

# Both scripts live in tests/, so the ssh plumbing is shared rather than duplicated.
from stage_yba_installer import (
    Fatal,
    SSH_KEY,
    SSH_OPTS,
    SSH_USERS,
    detect_ssh_user,
    log,
    remote_home,
    run,
)

CERT_NAME = "server_cert.pem"
KEY_NAME = "server_key.pem"
CERT_ORG = "Yugabyte yba-installer test"
DEFAULT_DAYS = 365
OUT_DIR = "~/downloads/test-certs"


def is_ip(name):
    try:
        ipaddress.ip_address(name)
        return True
    except ValueError:
        return False


def san_entry(name):
    """A SAN matches an address only as an IP entry, and a hostname only as DNS."""
    return f"IP:{name}" if is_ip(name) else f"DNS:{name}"


def generate_certs(name, mode, days, out_dir):
    """Write a self signed cert/key pair carrying name according to mode."""
    cert = out_dir / CERT_NAME
    key = out_dir / KEY_NAME
    subject = f"/O={CERT_ORG}"
    if mode == "cn":
        subject += f"/CN={name}"

    cmd = ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
           "-days", str(days), "-keyout", key, "-out", cert, "-subj", subject]
    if mode == "san":
        # -addext needs openssl 1.1.1+, which yba-installer already requires.
        cmd += ["-addext", f"subjectAltName={san_entry(name)}"]

    log(f"generating a cert with {describe_mode(mode, name)}")
    run(cmd, capture=True)
    key.chmod(0o600)
    return cert, key


def describe_mode(mode, name):
    if mode == "san":
        return f"SAN {san_entry(name)} and no Common Name"
    if mode == "cn":
        return f"Common Name {name} and no SAN"
    return "no SAN and no Common Name"


def expected_result(mode, name, target):
    """What the server-cert-hostname check should report for this cert."""
    if mode == "blank":
        return ("pass -- a cert with no names to check is left alone, with a "
                "'has no SAN or CN' warning in the log")
    if name == target:
        slot = "SAN" if mode == "san" else "Common Name"
        return f"pass -- the {slot} covers {target}"
    return (f"FAIL at a critical level -- the cert is issued for {name}, "
            f"but host is {target}")


def show_certs(cert):
    """Print the parts of the cert the check actually looks at."""
    subject = run(["openssl", "x509", "-noout", "-subject", "-in", cert],
                  capture=True, echo=False).stdout.strip()
    log(subject)
    # Missing extension is an expected outcome here, not an error.
    sans = run(["openssl", "x509", "-noout", "-ext", "subjectAltName", "-in", cert],
               capture=True, check=False, echo=False).stdout.strip()
    log(sans.replace("\n", " ").strip() if sans else "no subjectAltName extension")


def copy_certs(cert, key, target, ssh_key, user, remote_dir):
    log(f"copying {CERT_NAME} and {KEY_NAME} to {user}@{target}:{remote_dir}")
    run(["scp", "-i", ssh_key] + SSH_OPTS
        + [cert, key, f"{user}@{target}:{remote_dir}/"])
    return f"{remote_dir}/{CERT_NAME}", f"{remote_dir}/{KEY_NAME}"


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "target",
        help="ip address or dns name to copy the certs to. Also the name the cert "
             "is issued for, unless --name says otherwise")
    names = parser.add_mutually_exclusive_group()
    names.add_argument(
        "--cn", action="store_true",
        help="put the name in the Common Name instead of a subject alternative name")
    names.add_argument(
        "--blank", action="store_true",
        help="issue the cert with no SAN and no Common Name")
    parser.add_argument(
        "--name",
        help="issue the cert for this address instead of the target, to check that a "
             "mismatched cert is rejected. Combine with --cn to put it in the "
             "Common Name")
    parser.add_argument(
        "--days", type=int, default=DEFAULT_DAYS,
        help=f"how long the cert stays valid (default {DEFAULT_DAYS})")
    parser.add_argument(
        "--ssh-key", default=SSH_KEY,
        help=f"ssh key for the target (default {SSH_KEY})")
    parser.add_argument(
        "--username",
        help=f"user to log in as, instead of trying {', '.join(SSH_USERS)}")
    parser.add_argument(
        "--remote-dir",
        help="directory to copy the certs into (default the target's home directory)")
    parser.add_argument(
        "--out-dir", default=OUT_DIR,
        help=f"where the certs are written locally (default {OUT_DIR})")
    args = parser.parse_args(argv)

    if args.blank and args.name:
        parser.error("--blank issues a cert with no names, so --name cannot apply")
    return args


def main(argv):
    args = parse_args(argv)
    if not shutil.which("openssl"):
        raise Fatal("openssl is not on PATH, so no certs can be generated")

    ssh_key = Path(args.ssh_key).expanduser()
    if not ssh_key.is_file():
        raise Fatal(f"ssh key {ssh_key} not found")

    mode = "blank" if args.blank else "cn" if args.cn else "san"
    name = args.name or args.target
    out_dir = Path(args.out_dir).expanduser()
    out_dir.mkdir(parents=True, exist_ok=True)

    user = args.username or detect_ssh_user(args.target, ssh_key, SSH_USERS)
    remote_dir = args.remote_dir or remote_home(args.target, ssh_key, user)

    cert, key = generate_certs(name, mode, args.days, out_dir)
    show_certs(cert)
    remote_cert, remote_key = copy_certs(
        cert, key, args.target, ssh_key, user, remote_dir)

    print()
    print(f"Copied a cert with {describe_mode(mode, name)} to {args.target}.")
    print("Set these in yba-ctl.yml on the target:")
    print(f'  host: "{args.target}"')
    print(f'  server_cert_path: "{remote_cert}"')
    print(f'  server_key_path: "{remote_key}"')
    print()
    print("Then, from the installer bundle directory:")
    print("  sudo ./yba-ctl preflight")
    print(f"server-cert-hostname should {expected_result(mode, name, args.target)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Fatal as err:
        print(f"[stage] error: {err}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n[stage] interrupted", file=sys.stderr)
        sys.exit(130)
