#!/usr/bin/env python3
#
# Copyright (c) YugabyteDB, Inc.
#
"""Stage a locally built yba-ctl on a test machine, ready for manual testing.

Takes the official yba_installer_full bundle for some version, swaps in the yba-ctl
built from this checkout, ships the result to a test VM and extracts it. What lands
on the VM is a normal installer bundle, so every yba-ctl workflow (install, upgrade,
reconfigure, replicated-migrate) can be exercised against local changes.

  # latest master build
  ./tests/stage_yba_installer.py 10.150.7.24

  # a specific version
  ./tests/stage_yba_installer.py 10.150.7.24 2.31.0.0-b321

The VM is reached over ssh as one of SSH_USERS with SSH_KEY. When no version is
given, the newest master build in s3://releases.yugabyte.com that actually carries an
installer bundle is used, so the aws cli must be able to read that bucket.
"""

import argparse
import hashlib
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

S3_BUCKET = "releases.yugabyte.com"
SSH_KEY = "~/.yugabyte/yb-dev-aws-2.pem"
# Dev VMs hand out one of these, depending on the image they were built from.
SSH_USERS = ("centos", "ec2-user")
DOWNLOAD_DIR = "~/downloads"

# Master builds are 2.<minor>.0.0-b<build>, e.g. 2.31.0.0-b321.
MASTER_DIR_RE = re.compile(r"^2\.(\d+)\.0\.0-b(\d+)$")
# An explicit version only has to end in -b<build>, so release lines work too.
VERSION_RE = re.compile(r"^[\d.]+-b(\d+)$")
# Build directories are cut before the bundle is published, and some never get one,
# so discovery walks back from the newest rather than trusting the first hit.
MAX_BUILD_PROBES = 25

SSH_OPTS = [
    # Dev VM addresses get recycled, so a changed host key is expected here.
    "-o", "StrictHostKeyChecking=no",
    "-o", "UserKnownHostsFile=/dev/null",
    "-o", "LogLevel=ERROR",
]


class Fatal(Exception):
    """An error worth reporting to the user without a traceback."""


def log(msg):
    print(f"[stage] {msg}", flush=True)


def run(cmd, cwd=None, capture=False, check=True, echo=True):
    """Run cmd. Output is streamed unless capture is set."""
    if echo:
        log("$ " + " ".join(shlex.quote(str(c)) for c in cmd))
    kwargs = {"cwd": cwd, "text": True}
    if capture:
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.PIPE
    proc = subprocess.run([str(c) for c in cmd], check=False, **kwargs)
    if check and proc.returncode != 0:
        detail = (proc.stderr or "").strip() if capture else ""
        raise Fatal(f"command failed with exit code {proc.returncode}: "
                    + " ".join(shlex.quote(str(c)) for c in cmd)
                    + (f"\n{detail}" if detail else ""))
    return proc


def bundle_name(version):
    return f"yba_installer_full-{version}-centos-x86_64.tar.gz"


def bundle_dir_name(version):
    return f"yba_installer_full-{version}"


def gzip_program():
    """pigz cuts the repack of a ~2GB bundle down to seconds. Fall back to gzip."""
    return "pigz" if shutil.which("pigz") else "gzip"


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


# --------------------------------------------------------------------------------
# version discovery
# --------------------------------------------------------------------------------

def aws(profile, *args, capture=True, check=True, echo=False):
    cmd = ["aws"]
    if profile:
        cmd += ["--profile", profile]
    return run(cmd + list(args), capture=capture, check=check, echo=echo)


def s3_dirs(prefix, profile):
    """Directory names S3 reports directly under the given key prefix."""
    proc = aws(profile, "s3", "ls", f"s3://{S3_BUCKET}/{prefix}")
    names = []
    for line in proc.stdout.splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[0] == "PRE":
            names.append(fields[1].rstrip("/"))
    return names


def bundle_published(version, profile):
    key = f"{version}/{bundle_name(version)}"
    proc = aws(profile, "s3", "ls", f"s3://{S3_BUCKET}/{key}", check=False)
    return proc.returncode == 0 and bool(proc.stdout.strip())


def latest_master_version(profile):
    """Newest 2.<minor>.0.0-b<build> in the bucket that has an installer bundle."""
    log(f"looking for the latest master build in s3://{S3_BUCKET}")
    minors = {int(m.group(1)) for m in
              (MASTER_DIR_RE.match(name) for name in s3_dirs("2.", profile)) if m}
    if not minors:
        raise Fatal(f"no 2.<minor>.0.0-b<build> directories found in s3://{S3_BUCKET}. "
                    "Check that the aws cli can read the bucket "
                    "(pass --aws-profile if credentials live in a named profile)")
    minor = max(minors)

    builds = sorted(
        (int(m.group(2)) for m in
         (MASTER_DIR_RE.match(name) for name in s3_dirs(f"2.{minor}.0.0-b", profile))
         if m and int(m.group(1)) == minor),
        reverse=True)
    log(f"latest master series is 2.{minor}, with {len(builds)} build(s) published")

    for build in builds[:MAX_BUILD_PROBES]:
        version = f"2.{minor}.0.0-b{build}"
        if bundle_published(version, profile):
            log(f"newest master build with an installer bundle is {version}")
            return version
        log(f"{version} has no installer bundle, trying the next oldest")
    raise Fatal(f"none of the newest {MAX_BUILD_PROBES} 2.{minor} builds have a "
                f"{bundle_name('<version>')} bundle")


def build_id_of(version):
    match = VERSION_RE.match(version)
    if not match:
        raise Fatal(f"version {version!r} does not look like <version>-b<build>, "
                    "e.g. 2.31.0.0-b321")
    return match.group(1)


# --------------------------------------------------------------------------------
# build, download, repack
# --------------------------------------------------------------------------------

def build_ybactl(repo_dir, version, build_id):
    """Build yba-ctl from this checkout at the given version."""
    binary = repo_dir / "bin" / "yba-ctl"
    # The bin/yba-ctl make target has no source prerequisites, so an existing binary
    # is considered up to date and local changes would be silently left out.
    if binary.exists():
        log(f"removing {binary} so make rebuilds it")
        binary.unlink()
    log(f"building yba-ctl for {version} (runs the unit tests too)")
    run(["make", "yba-ctl", f"VERSION={version}", f"BUILD_ID={build_id}"], cwd=repo_dir)
    if not binary.exists():
        raise Fatal(f"make finished but {binary} is missing")
    return binary


def published_sha256(version, profile, dest_dir):
    """Digest S3 publishes next to the bundle, or None when it is absent."""
    key = f"{version}/{bundle_name(version)}.sha"
    local = dest_dir / f"{bundle_name(version)}.sha"
    proc = aws(profile, "s3", "cp", f"s3://{S3_BUCKET}/{key}", str(local), check=False)
    if proc.returncode != 0 or not local.exists():
        return None
    fields = local.read_text().split()
    return fields[0] if fields else None


def download_bundle(version, download_dir, profile, verify):
    """Fetch the official bundle, reusing a previous download when it is intact."""
    dest = download_dir / bundle_name(version)
    expected = published_sha256(version, profile, download_dir) if verify else None

    if dest.exists() and dest.stat().st_size > 0:
        if expected is None:
            log(f"reusing cached bundle {dest}")
            return dest
        log(f"checking the cached bundle at {dest}")
        if sha256_file(dest) == expected:
            log("cached bundle matches the published digest")
            return dest
        log("cached bundle does not match the published digest, downloading it again")
        dest.unlink()

    # Download to a scratch name so an interrupted transfer is never mistaken for a
    # usable cache entry on the next run.
    partial = download_dir / (dest.name + ".part")
    partial.unlink(missing_ok=True)
    log(f"downloading s3://{S3_BUCKET}/{version}/{dest.name} (about 2GB)")
    # The transfer meter redraws with carriage returns, which is handy on a terminal
    # but turns a redirected log into hundreds of KB of progress lines.
    progress = [] if sys.stdout.isatty() else ["--no-progress"]
    aws(profile, "s3", "cp", f"s3://{S3_BUCKET}/{version}/{dest.name}", str(partial),
        *progress, capture=False, echo=True)
    if expected is not None and sha256_file(partial) != expected:
        raise Fatal(f"downloaded bundle does not match the digest published at "
                    f"s3://{S3_BUCKET}/{version}/{dest.name}.sha")
    partial.rename(dest)
    return dest


def repack_bundle(bundle, ybactl, version, work_dir):
    """Extract the bundle, swap in ybactl and tar it back up."""
    gzip_prog = gzip_program()
    extract_root = work_dir / "extract"
    shutil.rmtree(extract_root, ignore_errors=True)
    extract_root.mkdir(parents=True)

    log(f"extracting {bundle.name}")
    run(["tar", "-I", gzip_prog, "-xf", bundle, "-C", extract_root])

    unpacked = extract_root / bundle_dir_name(version)
    target = unpacked / "yba-ctl"
    if not target.is_file():
        raise Fatal(f"{bundle.name} does not contain "
                    f"{bundle_dir_name(version)}/yba-ctl as expected")

    log(f"replacing {bundle_dir_name(version)}/yba-ctl with {ybactl}")
    shutil.copy2(ybactl, target)
    target.chmod(0o755)

    repacked = work_dir / bundle.name
    repacked.unlink(missing_ok=True)
    log(f"repacking into {repacked}")
    # -1 because this bundle only has to cross a LAN, not get published.
    run(["tar", "-I", f"{gzip_prog} -1", "-cf", repacked,
         "-C", extract_root, unpacked.name])
    shutil.rmtree(extract_root, ignore_errors=True)
    return repacked


# --------------------------------------------------------------------------------
# deploy
# --------------------------------------------------------------------------------

def ssh_cmd(key, user, host, remote_command, batch=False):
    cmd = ["ssh", "-i", key] + SSH_OPTS
    if batch:
        cmd += ["-o", "BatchMode=yes", "-o", "ConnectTimeout=10"]
    return cmd + [f"{user}@{host}", remote_command]


def detect_ssh_user(host, key, users):
    for user in users:
        proc = run(ssh_cmd(key, user, host, "true", batch=True),
                   capture=True, check=False, echo=False)
        if proc.returncode == 0:
            log(f"reaching {host} as {user}")
            return user
        log(f"cannot log in to {host} as {user}")
    raise Fatal(f"could not ssh to {host} as any of {', '.join(users)} using {key}")


def remote_home(host, key, user):
    proc = run(ssh_cmd(key, user, host, "echo $HOME"), capture=True, echo=False)
    home = proc.stdout.strip()
    if not home:
        raise Fatal(f"could not determine the home directory of {user}@{host}")
    return home


def deploy(repacked, version, host, key, user, remote_dir):
    """Copy the bundle to the target and extract it, replacing any earlier copy."""
    remote_bundle = f"{remote_dir}/{repacked.name}"
    remote_unpacked = f"{remote_dir}/{bundle_dir_name(version)}"

    log(f"copying {repacked.name} to {user}@{host}:{remote_dir}")
    run(["scp", "-i", key] + SSH_OPTS + [repacked, f"{user}@{host}:{remote_dir}/"])

    log(f"extracting on {host}, discarding any existing copy of {version}")
    script = "; ".join([
        "set -e",
        f"rm -rf {shlex.quote(remote_unpacked)}",
        f"tar -xzf {shlex.quote(remote_bundle)} -C {shlex.quote(remote_dir)}",
        f"sha256sum {shlex.quote(remote_unpacked + '/yba-ctl')}",
    ])
    proc = run(ssh_cmd(key, user, host, script), capture=True)
    remote_sha = proc.stdout.split()[0] if proc.stdout.split() else ""
    return remote_unpacked, remote_sha


# --------------------------------------------------------------------------------

def parse_args(argv):
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "target",
        help="ip address or dns name of the machine to stage the bundle on")
    parser.add_argument(
        "version", nargs="?",
        help="version to build and stage, e.g. 2.31.0.0-b321. Defaults to the "
             "newest master build in s3 that has an installer bundle")
    parser.add_argument(
        "--ssh-key", default=SSH_KEY,
        help=f"ssh key for the target (default {SSH_KEY})")
    parser.add_argument(
        "--username",
        help=f"user to log in as, instead of trying {', '.join(SSH_USERS)}")
    parser.add_argument(
        "--aws-profile", default=os.environ.get("AWS_PROFILE"),
        help="aws cli profile to read the releases bucket with "
             "(default $AWS_PROFILE, else the cli default)")
    parser.add_argument(
        "--download-dir", default=DOWNLOAD_DIR,
        help=f"where official bundles are cached (default {DOWNLOAD_DIR})")
    parser.add_argument(
        "--remote-dir",
        help="directory to stage into on the target (default its home directory)")
    parser.add_argument(
        "--skip-checksum", action="store_true",
        help="do not verify downloads against the digest published in s3")
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    started = time.monotonic()

    repo_dir = Path(__file__).resolve().parent.parent
    key = Path(args.ssh_key).expanduser()
    if not key.is_file():
        raise Fatal(f"ssh key {key} not found")
    download_dir = Path(args.download_dir).expanduser()
    download_dir.mkdir(parents=True, exist_ok=True)

    version = args.version or latest_master_version(args.aws_profile)
    build_id = build_id_of(version)
    if args.version and not bundle_published(version, args.aws_profile):
        raise Fatal(f"s3://{S3_BUCKET}/{version}/{bundle_name(version)} does not "
                    "exist, so there is no bundle to build on")

    user = args.username or detect_ssh_user(args.target, key, SSH_USERS)
    remote_dir = args.remote_dir or remote_home(args.target, key, user)

    ybactl = build_ybactl(repo_dir, version, build_id)
    local_sha = sha256_file(ybactl)
    bundle = download_bundle(version, download_dir, args.aws_profile,
                             verify=not args.skip_checksum)

    # Repack beside the cache so the pristine download stays reusable.
    work_dir = download_dir / "staged"
    work_dir.mkdir(parents=True, exist_ok=True)
    repacked = repack_bundle(bundle, ybactl, version, work_dir)

    remote_unpacked, remote_sha = deploy(
        repacked, version, args.target, key, user, remote_dir)
    if remote_sha != local_sha:
        raise Fatal("the yba-ctl on the target does not match the one just built "
                    f"(local {local_sha[:12]}, remote {remote_sha[:12] or 'unknown'})")

    elapsed = int(time.monotonic() - started)
    log(f"done in {elapsed // 60}m{elapsed % 60:02d}s, "
        f"yba-ctl sha256 {local_sha[:12]} verified on the target")
    print()
    print(f"Staged {version} with a locally built yba-ctl on {args.target}:")
    print(f"  ssh -i {key} {user}@{args.target}")
    print(f"  cd {remote_unpacked}")
    print("  sudo ./yba-ctl install")
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
