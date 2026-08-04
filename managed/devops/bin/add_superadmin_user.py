#!/usr/bin/env python
"""
Insert a local SuperAdmin user directly into the YBA application database.

Uses the same bcrypt format (cost 12) as Spring BCrypt / BcOpenBsdHasher, which
Users.authWithPassword accepts for hashes that do not start with '4.' (PBKDF2).

Also inserts principal + role_binding rows matching registration when new RBAC is enabled.

Pattern copied from edit_universe_details.py (psql invocation for standalone/docker/k8s).

Customer UUID resolution (`--customer-uuid` optional):

- ``yb.multiTenant`` is static JVM config (not stored in Postgres). When there is exactly one row
  in ``customer``, that UUID is used (typical single-tenant install).
- When multiple ``customer`` rows exist, behavior matches Java ``customerCount > 1``: you must
  either pass ``--customer-uuid`` or configure the global runtime key
  ``yb.security.ldap.ldap_customeruuid`` (same LDAP default tenant as ``LdapUtil``). Optional
  ``--application-conf`` parses ``multiTenant`` for logging context only.

Requires: Python 3, bcrypt (included in the yb devops venv / PEX; yb_devops_home/bin/py_wrapper.sh).

Examples:
standalone install:
  yb_devops_home/bin/py_wrapper.sh yb_devops_home/bin/add_superadmin_user.py
  --email admin@example.com --password 'Secret123!' -t standalone

  yb_devops_home/bin/py_wrapper.sh yb_devops_home/bin/add_superadmin_user.py
   -e admin@example.com -p 'Secret123!' -t standalone \\
      --customer-uuid 11111111-2222-3333-4444-555555555555 \\
      --application-conf ../yugaware/conf/application.conf

docker install:
export DOCKER_POSTGRES_CONTAINER=yugaware-postgres
export POSTGRES_USER=postgres
export POSTGRES_DB=yugaware
export POSTGRES_HOST=localhost
export POSTGRES_PORT=5432
  yb_devops_home/bin/py_wrapper.sh yb_devops_home/bin/add_superadmin_user.py
   -e admin@example.com -p 'Secret123!' \\
   -t docker

"""
from __future__ import print_function

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import uuid

import bcrypt

# Matches GlobalConfKeys / LdapUtil global LDAP default customer (multi-tenant + LDAP).
RUNTIME_LDAP_CUSTOMER_UUID_PATH = "yb.security.ldap.ldap_customeruuid"
# Global runtime scope row (scoped_runtime_config).
GLOBAL_RUNTIME_SCOPE_UUID = "00000000-0000-0000-0000-000000000000"

# application.conf (HOCON-ish); line-anchored, same intent as prior whole-file regex.
_YB_MULTI_TENANT_LINE = re.compile(r"\s*yb\.multiTenant\s*=\s*(true|false)\b", re.I)
_MULTI_TENANT_LINE = re.compile(r"\s*multiTenant\s*=\s*(true|false)\b", re.I)


def msg_exit(msg, code=1):
    print(msg, file=sys.stderr)
    sys.exit(code)


def get_kubectl_cmd_prefix():
    kubectl_cmd = ["kubectl"]
    if args.namespace:
        kubectl_cmd.extend(["-n", args.namespace])
    if args.kubeconfig:
        kubectl_cmd.extend(["--kubeconfig", args.kubeconfig])
    return kubectl_cmd


def get_running_pod(kubectl_cmd_prefix):
    pod_cmd = " ".join(kubectl_cmd_prefix) + " get pods | awk '($3 ~\"Running\"){print $1; exit}'"
    pod_name = str(subprocess.check_output(pod_cmd, shell=True).decode("utf-8")).strip()
    if not pod_name:
        msg_exit("No running pod is found.")
    print("Found running pod: {}".format(pod_name))
    return pod_name


def remote_copy_psql_query(pod_name, psql_query):
    (_, sql_update_filepath) = tempfile.mkstemp(prefix="sql_update")
    sql_update_filepath = "{}.sql".format(sql_update_filepath)
    with open(sql_update_filepath, "w") as sql_update_file:
        sql_update_file.write(str(psql_query))
    kubectl_cmd_prefix = get_kubectl_cmd_prefix()
    sql_filename = os.path.basename(sql_update_filepath)
    remote_sql_filepath = "/tmp/{}".format(sql_filename)
    copy_cmd = kubectl_cmd_prefix
    copy_cmd.extend(
        ["cp", sql_update_filepath, "{}:{}".format(pod_name, remote_sql_filepath), "-c", "postgres"]
    )
    print("Running {}".format(" ".join(copy_cmd)))
    subprocess.check_output(copy_cmd)
    return remote_sql_filepath


def sql_escape_literal(value):
    """Return a single-quoted SQL string literal; doubles embedded single quotes."""
    if value is None:
        return "NULL"
    return "'" + str(value).replace("'", "''") + "'"


def run_psql(psql_query):
    """Run SQL via psql (dynamic values must already be embedded as sql_escape_literal(...))."""
    psql_common_args = [
        "-U",
        os.environ.get("POSTGRES_USER", "postgres"),
        "-d",
        os.environ.get("POSTGRES_DB", "yugaware"),
        "-h",
        os.environ.get("POSTGRES_HOST", "localhost"),
        "-p",
        os.environ.get("POSTGRES_PORT", "5432"),
        "-t",
    ]
    if args.install_type == "standalone":
        psql_cmd = ["psql"]
        psql_cmd.extend(psql_common_args)
        psql_cmd.extend(["-c", psql_query])
    elif args.install_type == "docker":
        if os.system("sudo docker ps -a | grep postgres > /dev/null") != 0:
            msg_exit("postgres docker container is not running.")
        docker_pg_container = os.environ.get("DOCKER_POSTGRES_CONTAINER", "postgres")
        psql_cmd = ["sudo", "docker", "exec", docker_pg_container, "psql"]
        psql_cmd.extend(psql_common_args)
        psql_cmd.extend(["-c", psql_query])
    elif args.install_type == "kubernetes":
        kubectl_cmd_prefix = get_kubectl_cmd_prefix()
        pod_name = args.pod
        if not pod_name:
            pod_name = get_running_pod(kubectl_cmd_prefix)
        remote_sql_filepath = remote_copy_psql_query(pod_name, psql_query)
        psql_cmd = kubectl_cmd_prefix
        psql_cmd.extend(["exec", pod_name, "-t", "-c", "postgres", "--", "psql"])
        psql_cmd.extend(psql_common_args)
        psql_cmd.extend(["-f", remote_sql_filepath])
    else:
        msg_exit("Unknown install type: {}.".format(args.install_type))
    try:
        raw = subprocess.check_output(psql_cmd, stderr=subprocess.STDOUT)
    except FileNotFoundError as e:
        msg_exit("Failed to start {}: {}".format(psql_cmd[0], e), code=2)
    except subprocess.CalledProcessError as e:
        detail = ""
        if e.output:
            detail = e.output.decode("utf-8", errors="replace").strip()
        msg_exit(
            "psql failed with exit code {}.{}{}".format(
                e.returncode,
                "\n" if detail else "",
                detail or "(no output captured)",
            ),
            code=2,
        )
    return str(raw.decode("utf-8")).strip()


def hash_password_bcrypt(password):
    # Match BcOpenBsdHasher: BCrypt.gensalt(12, rnd)
    salt = bcrypt.gensalt(rounds=12)
    return bcrypt.hashpw(password.encode("utf-8"), salt).decode("ascii")


def build_resource_group_json(customer_uuid):
    defs = []
    for rt in ("UNIVERSE", "ROLE", "USER"):
        defs.append(
            {
                "resourceType": rt,
                "allowAll": True,
                "resourceUUIDSet": [],
            }
        )
    defs.append(
        {
            "resourceType": "OTHER",
            "allowAll": False,
            "resourceUUIDSet": [str(customer_uuid)],
        }
    )
    # Ebean @DbJson ResourceGroup shape
    return json.dumps({"resourceDefinitionSet": defs}, separators=(",", ":"))


def fetch_single_value(query):
    # Errors: run_psql uses check_output and msg_exit on failure; no extra handling needed here.
    out = run_psql(query).strip()
    if not out:
        return None
    lines = [ln.strip() for ln in out.splitlines() if ln.strip()]
    return lines[-1] if lines else None


def read_multi_tenant_from_application_conf(conf_path):
    """
    Parse yb.multiTenant / multiTenant from a Play HOCON-style file (e.g. application.conf).
    Returns True, False, or None if not found.

    Scans line-by-line (no full-file slurp). If any line sets yb.multiTenant, that wins; else
    the first line setting multiTenant is used.
    """
    if not conf_path or not os.path.isfile(conf_path):
        return None
    try:
        with open(conf_path, "r") as f:
            for line in f:
                m = _YB_MULTI_TENANT_LINE.match(line)
                if m:
                    return m.group(1).lower() == "true"
            f.seek(0)
            for line in f:
                m = _MULTI_TENANT_LINE.match(line)
                if m:
                    return m.group(1).lower() == "true"
    except OSError as e:
        msg_exit("Cannot read {}: {}".format(conf_path, e), code=2)
    return None


def normalize_uuid_string(val):
    if val is None:
        return None
    s = str(val).strip().strip('"').strip("'")
    if not s:
        return None
    try:
        return str(uuid.UUID(s))
    except ValueError:
        return None


def fetch_ldap_runtime_customer_uuid():
    """LDAP multi-tenant default customer UUID stored in runtime_config_entry (same as YBA UI)."""
    row = fetch_single_value(
        "SELECT value FROM runtime_config_entry WHERE scope_uuid = '{}' "
        "AND path = '{}';".format(GLOBAL_RUNTIME_SCOPE_UUID, RUNTIME_LDAP_CUSTOMER_UUID_PATH)
    )
    return normalize_uuid_string(row)


def customer_uuid_exists(cu):
    n = fetch_single_value(
        "SELECT COUNT(*)::text FROM customer WHERE uuid = {}::uuid;".format(sql_escape_literal(cu))
    )
    return n == "1"


def resolve_customer_uuid():
    """
    Determine customer_uuid for the new user.

    - yb.multiTenant is JVM static config (application.conf); it is not reliably in Postgres.
    - We infer "multiple tenants in DB" from COUNT(customer) > 1
      (aligned with ThirdPartyLoginHandler).
    - When multiple customer rows exist, we try runtime_config ldap_customeruuid next
      (see LdapUtil).

    Returns (customer_uuid_str, summary_message).
    """
    if args.customer_uuid:
        cu = normalize_uuid_string(args.customer_uuid)
        if not cu:
            msg_exit("Invalid --customer-uuid.", code=2)
        if not customer_uuid_exists(cu):
            msg_exit("No customer row exists for UUID {}.".format(cu), code=2)
        return cu, "Using explicit --customer-uuid."

    cnt = fetch_single_value("SELECT COUNT(*)::text FROM customer;")
    if cnt == "0" or cnt is None:
        msg_exit("No rows in customer table.", code=2)

    mt_conf = None
    if args.application_conf:
        mt_conf = read_multi_tenant_from_application_conf(args.application_conf)
    else:
        application_conf = os.path.join(
            os.path.dirname(__file__), "..", "..", "yugaware", "conf", "application.conf"
        )
        print("default application_conf path: {}".format(application_conf))
        if not os.path.isfile(application_conf):
            msg_exit("Application conf file not found. please pass --application-conf", code=2)
        mt_conf = read_multi_tenant_from_application_conf(application_conf)
        print("mt_conf: {}".format(mt_conf))
    if cnt == "1":
        cu = fetch_single_value("SELECT uuid::text FROM customer LIMIT 1;")
        if not cu:
            msg_exit("No customer row found.", code=2)
        msg_parts = [
            "Single customer row in database (typical single-tenant install; "
            "yb.multiTenant is usually false)."
        ]
        if mt_conf is True:
            msg_parts.append(
                "Note: {} reports yb.multiTenant=true but only one customer exists - "
                "using that customer.".format(args.application_conf)
            )
        elif mt_conf is False:
            msg_parts.append(
                "{} indicates yb.multiTenant=false.".format(args.application_conf)
            )
        return cu, " ".join(msg_parts)

    # Multiple customer rows - same disambiguation pressure as multi-tenant + LDAP in LdapUtil.
    msg_parts = [
        (
            "Multiple customer rows (count={}) - multi-tenant-style deployment; "
            "must choose a tenant."
        ).format(cnt)
    ]
    if mt_conf is not None:
        msg_parts.append(
            "application.conf multiTenant={} (informational only).".format(mt_conf)
        )

    ldap_cu = fetch_ldap_runtime_customer_uuid()
    if ldap_cu and customer_uuid_exists(ldap_cu):
        print("ldap_cu: {}".format(ldap_cu))
        msg_parts.append(
            "Using customer UUID from runtime_config_entry path '{}' "
            "(LDAP customer default).".format(RUNTIME_LDAP_CUSTOMER_UUID_PATH)
        )
        return ldap_cu, " ".join(msg_parts)

    msg_exit(
        "Multiple customers present and no usable default.\n"
        "  Set runtime config '{}' to a valid customer UUID, or pass --customer-uuid.\n"
        "  (Same source as YBA LDAP multi-tenant default - see GlobalConfKeys / LdapUtil.)".format(
            RUNTIME_LDAP_CUSTOMER_UUID_PATH
        ),
        code=2,
    )


def main():
    email = args.email.strip().lower()

    if args.password_hash:
        password_hash = args.password_hash
    else:
        password_hash = hash_password_bcrypt(args.password)

    customer_uuid, customer_msg = resolve_customer_uuid()
    print(customer_msg)

    primary_superadmin_count = fetch_single_value(
        "SELECT COUNT(*)::text FROM users WHERE customer_uuid = {}::uuid "
        "AND is_primary = TRUE AND role = 'SuperAdmin';".format(
            sql_escape_literal(customer_uuid)
        )
    )
    if primary_superadmin_count and primary_superadmin_count != "0":
        print("A primary SuperAdmin user already exists")
        return

    exists = fetch_single_value(
        "SELECT COUNT(*)::text FROM users WHERE lower(email) = lower({});".format(
            sql_escape_literal(email)
        )
    )
    if exists and exists != "0":
        msg_exit("User with email {} already exists.".format(email), code=2)

    role_uuid = fetch_single_value(
        "SELECT role_uuid::text FROM role WHERE customer_uuid = {}::uuid "
        "AND name = 'SuperAdmin';".format(sql_escape_literal(customer_uuid))
    )
    if not role_uuid:
        msg_exit(
            (
                "SuperAdmin role row not found for customer {}. "
                "Run DB migrations / sync system roles."
            ).format(customer_uuid),
            code=2,
        )

    user_uuid = uuid.uuid4()
    rb_uuid = uuid.uuid4()
    now_sql = "(CURRENT_TIMESTAMP AT TIME ZONE 'UTC')"

    resource_group_json = build_resource_group_json(customer_uuid)
    lit_user = sql_escape_literal(str(user_uuid))
    lit_cu = sql_escape_literal(customer_uuid)
    lit_email = sql_escape_literal(email)
    lit_pw = sql_escape_literal(password_hash)
    lit_role = sql_escape_literal(role_uuid)
    lit_rb = sql_escape_literal(str(rb_uuid))
    lit_rg = sql_escape_literal(resource_group_json)

    stmts = [
        """INSERT INTO users (
    uuid,
    customer_uuid,
    email,
    password_hash,
    creation_date,
    role,
    is_primary,
    user_type,
    ldap_specified_role,
    api_token_version
) VALUES (
    {u}::uuid,
    {c}::uuid,
    lower({e}),
    {p},
    {n},
    'SuperAdmin',
    FALSE,
    'local',
    FALSE,
    0
);""".format(u=lit_user, c=lit_cu, e=lit_email, p=lit_pw, n=now_sql),
        """INSERT INTO principal (uuid, user_uuid, group_uuid, type)
VALUES ({u}::uuid, {u}::uuid, NULL, 'USER');""".format(u=lit_user),
    ]

    if not args.skip_role_binding:
        stmts.append(
            """INSERT INTO role_binding (
    uuid,
    principal_uuid,
    type,
    role_uuid,
    create_time,
    update_time,
    resource_group
) VALUES (
    {rb}::uuid,
    {u}::uuid,
    'System',
    {role}::uuid,
    {n},
    {n},
    {rg}::json
);""".format(rb=lit_rb, u=lit_user, role=lit_role, rg=lit_rg, n=now_sql)
        )

    sql_block = "BEGIN;\n" + "\n".join(stmts) + "\nCOMMIT;\n"

    if args.dry_run:
        print(sql_block)
        return

    print(run_psql(sql_block))
    print(
        "Inserted SuperAdmin user uuid={} email={} for customer {}.".format(
            user_uuid, email, customer_uuid
        )
    )


if os.path.exists("/.dockerenv"):
    msg_exit(
        "Run this script from the docker host (not inside the yugaware container), "
        "same as edit_universe_details.py.",
        code=2,
    )

install_types = ["standalone", "docker", "kubernetes"]


class CustomHelpFormatter(
    argparse.ArgumentDefaultsHelpFormatter, argparse.RawDescriptionHelpFormatter
):
    pass


parser = argparse.ArgumentParser(
    formatter_class=CustomHelpFormatter,
    description="Insert a SuperAdmin user into YBA Postgres (users + principal + role_binding).",
    epilog="""\
Parameter notes:
  - Mandatory arguments: --email , --password and --install-type
    Examples: add_superadmin_user.py -e admin@example.com -p 'Secret123!' -t standalone
              add_superadmin_user.py -e admin@example.com -p 'Secret123!' -t docker
              add_superadmin_user.py -e admin@example.com -p 'Secret123!' -t kubernetes

  - --application-conf is optional:
      * If not provided, the script will default to $devops_home/../yugaware/conf/application.conf.
  - --customer-uuid is optional:
      * If there is exactly one customer row, that UUID is used automatically.
      * If there are multiple customer rows, script uses runtime config key
        yb.security.ldap.ldap_customeruuid or exits unless --customer-uuid is provided.
  - For kubernetes installs, --namespace / --kubeconfig / --pod are optional.

Examples:
To be run from $yb_devops_home/
cd $yb_devops_home/

  Standalone:
    ./bin/py_wrapper.sh ./bin/add_superadmin_user.py
    --email admin@example.com --password 'Secret123!' -t standalone

  Docker:
    export DOCKER_POSTGRES_CONTAINER=yugaware-postgres
    export POSTGRES_USER=postgres
    export POSTGRES_DB=yugaware
    export POSTGRES_HOST=localhost
    export POSTGRES_PORT=5432
    ./bin/py_wrapper.sh ./bin/add_superadmin_user.py
    --email admin@example.com --password 'Secret123!' -t docker

  Kubernetes:
    ./bin/py_wrapper.sh ./bin/add_superadmin_user.py \\
    -t kubernetes -e admin@example.com -p 'Secret123!' \\
      -n yb-platform -f /path/to/kubeconfig
""",
)
parser.add_argument("-e", "--email", required=True, help="Login email for the new user.")
pw_group = parser.add_mutually_exclusive_group(required=True)
pw_group.add_argument("-p", "--password", help="Plain-text password (hashed with bcrypt cost 12).")
pw_group.add_argument(
    "--password-hash",
    help="Pre-computed password_hash string (bcrypt or PBKDF2 '4.' format). Overrides --password.",
)
parser.add_argument(
    "-c",
    "--customer-uuid",
    help="Customer (tenant) UUID. If omitted: single customer row -> use it; multiple rows -> "
    "try yb.security.ldap.ldap_customeruuid from runtime_config_entry, else fail.",
)
parser.add_argument(
    "--application-conf",
    metavar="PATH",
    help="Optional YBA application.conf (or fragment) to parse yb.multiTenant / multiTenant "
    "(informational logging only; DB customer count drives disambiguation). "
    "example path: $devops_home/../yugaware/conf/application.conf",
)
parser.add_argument(
    "-t",
    "--install-type",
    choices=install_types,
    default=install_types[0],
    help="How to reach Postgres.",
)
parser.add_argument("-n", "--namespace", help="Kubernetes namespace.", required=False)
parser.add_argument("-f", "--kubeconfig", help="Kubernetes kube config filepath.", required=False)
parser.add_argument("-P", "--pod", help="YBA Postgres pod name (kubernetes only).", required=False)
parser.add_argument(
    "--skip-role-binding",
    action="store_true",
    help="Only insert users + principal (for installs without new RBAC / troubleshooting).",
)
parser.add_argument(
    "--dry-run",
    action="store_true",
    help="Print SQL and exit without executing.",
)
args = parser.parse_args()

if __name__ == "__main__":
    main()
