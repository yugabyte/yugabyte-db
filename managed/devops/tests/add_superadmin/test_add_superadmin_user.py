#!/bin/python3

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import mock_open
from unittest.mock import patch


SCRIPT_PATH = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "bin", "add_superadmin_user.py")
)


def _load_script_module():
    if "bcrypt" not in sys.modules:
        sys.modules["bcrypt"] = SimpleNamespace(
            gensalt=lambda rounds=12: b"salt",
            hashpw=lambda raw, salt: b"fake-hash",
        )
    spec = importlib.util.spec_from_file_location("add_superadmin_user_script", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TestAddSuperadminUserScript(TestCase):
    @classmethod
    def setUpClass(cls):
        cls.script = _load_script_module()

    def setUp(self):
        self.old_args = self.script.args

    def tearDown(self):
        self.script.args = self.old_args

    def _args(self, **kwargs):
        base = {
            "email": "admin@example.com",
            "password_hash": "hash",
            "password": "pw",
            "customer_uuid": None,
            "application_conf": None,
            "install_type": "standalone",
            "namespace": None,
            "kubeconfig": None,
            "pod": None,
            "skip_role_binding": False,
            "dry_run": False,
        }
        base.update(kwargs)
        return SimpleNamespace(**base)

    def test_msg_exit_raises(self):
        with self.assertRaises(SystemExit) as ex:
            self.script.msg_exit("boom", code=2)
        self.assertEqual(ex.exception.code, 2)

    def test_get_kubectl_cmd_prefix(self):
        self.script.args = self._args(namespace="ns", kubeconfig="/tmp/k")
        self.assertEqual(
            self.script.get_kubectl_cmd_prefix(),
            ["kubectl", "-n", "ns", "--kubeconfig", "/tmp/k"],
        )

    def test_get_running_pod(self):
        with patch.object(self.script.subprocess, "check_output", return_value=b"pod-a\n"):
            pod = self.script.get_running_pod(["kubectl"])
        self.assertEqual(pod, "pod-a")

    def test_get_running_pod_no_pod_exits(self):
        with patch.object(
            self.script.subprocess, "check_output", return_value=b"   "
        ), self.assertRaises(SystemExit):
            self.script.get_running_pod(["kubectl"])

    def test_remote_copy_psql_query(self):
        with patch.object(
            self.script.tempfile, "mkstemp", return_value=(1, "/tmp/sql_update123")
        ), patch("builtins.open", mock_open()), patch.object(
            self.script, "get_kubectl_cmd_prefix", return_value=["kubectl", "-n", "ns"]
        ), patch.object(
            self.script.subprocess, "check_output"
        ) as mock_check:
            remote = self.script.remote_copy_psql_query("pod1", "select 1;")

        self.assertEqual(remote, "/tmp/sql_update123.sql")
        cmd = mock_check.call_args[0][0]
        self.assertIn("cp", cmd)
        self.assertIn("pod1:/tmp/sql_update123.sql", cmd)

    def test_sql_escape_literal(self):
        self.assertEqual(self.script.sql_escape_literal(None), "NULL")
        self.assertEqual(self.script.sql_escape_literal("abc"), "'abc'")
        self.assertEqual(self.script.sql_escape_literal("a'b"), "'a''b'")

    def test_run_psql_standalone(self):
        self.script.args = self._args(install_type="standalone")
        with patch.object(
            self.script.subprocess, "check_output", return_value=b"ok\n"
        ) as mock_check:
            out = self.script.run_psql("select 1;")
        self.assertEqual(out, "ok")
        self.assertEqual(mock_check.call_args[0][0][0], "psql")

    def test_run_psql_docker_container_missing(self):
        self.script.args = self._args(install_type="docker")
        with patch.object(self.script.os, "system", return_value=1), self.assertRaises(SystemExit):
            self.script.run_psql("select 1;")

    def test_run_psql_docker_success(self):
        self.script.args = self._args(install_type="docker")
        with patch.object(self.script.os, "system", return_value=0), patch.dict(
            self.script.os.environ, {"DOCKER_POSTGRES_CONTAINER": "pg-c"}, clear=False
        ), patch.object(
            self.script.subprocess, "check_output", return_value=b" 1 \n"
        ) as mock_check:
            out = self.script.run_psql("select 1;")
        self.assertEqual(out, "1")
        self.assertEqual(mock_check.call_args[0][0][:4], ["sudo", "docker", "exec", "pg-c"])

    def test_run_psql_kubernetes_with_explicit_pod(self):
        self.script.args = self._args(install_type="kubernetes", pod="pod-x")
        with patch.object(
            self.script, "get_kubectl_cmd_prefix", return_value=["kubectl", "-n", "ns"]
        ), patch.object(
            self.script, "remote_copy_psql_query", return_value="/tmp/q.sql"
        ), patch.object(
            self.script.subprocess, "check_output", return_value=b"done\n"
        ) as mock_check:
            out = self.script.run_psql("select 1;")
        self.assertEqual(out, "done")
        self.assertIn("-f", mock_check.call_args[0][0])
        self.assertIn("/tmp/q.sql", mock_check.call_args[0][0])

    def test_run_psql_file_not_found_exits(self):
        self.script.args = self._args(install_type="standalone")
        with patch.object(
            self.script.subprocess, "check_output", side_effect=FileNotFoundError("missing")
        ), self.assertRaises(SystemExit):
            self.script.run_psql("select 1;")

    def test_run_psql_called_process_error_exits(self):
        self.script.args = self._args(install_type="standalone")
        err = subprocess.CalledProcessError(1, ["psql"], output=b"bad syntax")
        with patch.object(
            self.script.subprocess, "check_output", side_effect=err
        ), self.assertRaises(SystemExit):
            self.script.run_psql("select 1;")

    def test_hash_password_bcrypt(self):
        with patch.object(self.script.bcrypt, "gensalt", return_value=b"salt") as gs, patch.object(
            self.script.bcrypt, "hashpw", return_value=b"hashed"
        ) as hp:
            out = self.script.hash_password_bcrypt("pw")
        self.assertEqual(out, "hashed")
        gs.assert_called_once_with(rounds=12)
        hp.assert_called_once()

    def test_build_resource_group_json(self):
        out = self.script.build_resource_group_json("11111111-2222-3333-4444-555555555555")
        decoded = json.loads(out)
        defs = decoded["resourceDefinitionSet"]
        self.assertEqual(len(defs), 4)
        self.assertEqual(defs[-1]["resourceType"], "OTHER")

    def test_fetch_single_value(self):
        with patch.object(self.script, "run_psql", return_value="\n a \n b \n"):
            self.assertEqual(self.script.fetch_single_value("q"), "b")
        with patch.object(self.script, "run_psql", return_value="   "):
            self.assertIsNone(self.script.fetch_single_value("q"))

    def test_read_multi_tenant_from_application_conf(self):
        with tempfile.NamedTemporaryFile(mode="w", delete=False) as tf:
            tf.write("multiTenant=false\n")
            tf.write("yb.multiTenant=true\n")
            conf_path = tf.name
        try:
            self.assertTrue(self.script.read_multi_tenant_from_application_conf(conf_path))
        finally:
            os.remove(conf_path)

    def test_read_multi_tenant_fallback_to_legacy_key(self):
        with tempfile.NamedTemporaryFile(mode="w", delete=False) as tf:
            tf.write("multiTenant=false\n")
            conf_path = tf.name
        try:
            self.assertFalse(self.script.read_multi_tenant_from_application_conf(conf_path))
        finally:
            os.remove(conf_path)

    def test_read_multi_tenant_open_error(self):
        with patch.object(self.script.os.path, "isfile", return_value=True), patch(
            "builtins.open", side_effect=OSError("nope")
        ), self.assertRaises(SystemExit):
            self.script.read_multi_tenant_from_application_conf("/tmp/x")

    def test_normalize_uuid_string(self):
        valid = "11111111-2222-3333-4444-555555555555"
        self.assertEqual(self.script.normalize_uuid_string("'" + valid + "'"), valid)
        self.assertIsNone(self.script.normalize_uuid_string("bad"))
        self.assertIsNone(self.script.normalize_uuid_string(None))

    def test_fetch_ldap_runtime_customer_uuid(self):
        with patch.object(
            self.script, "fetch_single_value", return_value='"11111111-2222-3333-4444-555555555555"'
        ) as fsv:
            out = self.script.fetch_ldap_runtime_customer_uuid()
        self.assertEqual(out, "11111111-2222-3333-4444-555555555555")
        self.assertIn("runtime_config_entry", fsv.call_args[0][0])

    def test_customer_uuid_exists(self):
        with patch.object(self.script, "fetch_single_value", return_value="1"):
            self.assertTrue(
                self.script.customer_uuid_exists("11111111-2222-3333-4444-555555555555")
            )
        with patch.object(self.script, "fetch_single_value", return_value="0"):
            self.assertFalse(
                self.script.customer_uuid_exists("11111111-2222-3333-4444-555555555555")
            )

    def test_resolve_customer_uuid_explicit(self):
        self.script.args = self._args(customer_uuid="11111111-2222-3333-4444-555555555555")
        with patch.object(self.script, "customer_uuid_exists", return_value=True):
            cu, msg = self.script.resolve_customer_uuid()
        self.assertEqual(cu, "11111111-2222-3333-4444-555555555555")
        self.assertIn("explicit", msg)

    def test_resolve_customer_uuid_single_customer(self):
        self.script.args = self._args(application_conf="/tmp/app.conf")
        with patch.object(
            self.script,
            "fetch_single_value",
            side_effect=["1", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"],
        ), patch.object(
            self.script, "read_multi_tenant_from_application_conf", return_value=False
        ):
            cu, msg = self.script.resolve_customer_uuid()
        self.assertEqual(cu, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
        self.assertIn("Single customer row", msg)

    def test_resolve_customer_uuid_multiple_customers_with_ldap_default(self):
        self.script.args = self._args(application_conf="/tmp/app.conf")
        with patch.object(self.script, "fetch_single_value", side_effect=["2"]), patch.object(
            self.script, "read_multi_tenant_from_application_conf", return_value=True
        ), patch.object(
            self.script,
            "fetch_ldap_runtime_customer_uuid",
            return_value="11111111-2222-3333-4444-555555555555",
        ), patch.object(self.script, "customer_uuid_exists", return_value=True):
            cu, msg = self.script.resolve_customer_uuid()
        self.assertEqual(cu, "11111111-2222-3333-4444-555555555555")
        self.assertIn("Multiple customer rows", msg)
        self.assertIn("LDAP customer default", msg)

    def test_resolve_customer_uuid_multiple_customers_no_default_exits(self):
        self.script.args = self._args(application_conf="/tmp/app.conf")
        with patch.object(self.script, "fetch_single_value", side_effect=["2"]), patch.object(
            self.script, "read_multi_tenant_from_application_conf", return_value=None
        ), patch.object(
            self.script, "fetch_ldap_runtime_customer_uuid", return_value=None
        ), self.assertRaises(SystemExit):
            self.script.resolve_customer_uuid()

    def test_main_dry_run_generates_escaped_sql(self):
        self.script.args = self._args(
            email="A'B@example.com",
            password_hash="hash'value",
            dry_run=True,
            install_type="standalone",
        )
        with patch.object(
            self.script,
            "resolve_customer_uuid",
            return_value=("11111111-2222-3333-4444-555555555555", "ok"),
        ), patch.object(
            self.script,
            "fetch_single_value",
            side_effect=["0", "0", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"],
        ), patch("builtins.print") as mocked_print:
            self.script.main()

        output = "\n".join(str(call.args[0]) for call in mocked_print.call_args_list if call.args)
        self.assertIn("BEGIN;", output)
        self.assertIn("lower('a''b@example.com')", output)
        self.assertIn("'hash''value'", output)
        self.assertNotIn(":'v_", output)

    def test_main_non_dry_run_invokes_run_psql(self):
        self.script.args = self._args(
            email="admin@example.com",
            password_hash="h",
            dry_run=False,
            skip_role_binding=True,
            install_type="standalone",
        )
        with patch.object(
            self.script,
            "resolve_customer_uuid",
            return_value=("11111111-2222-3333-4444-555555555555", "ok"),
        ), patch.object(
            self.script,
            "fetch_single_value",
            side_effect=["0", "0", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"],
        ), patch.object(
            self.script, "run_psql", return_value="OK"
        ) as rp, patch.object(
            self.script.uuid, "uuid4", side_effect=["u-1", "rb-1"]
        ):
            self.script.main()
        self.assertEqual(rp.call_count, 1)
        sql = rp.call_args[0][0]
        self.assertIn("INSERT INTO users", sql)
        self.assertIn("INSERT INTO principal", sql)
        self.assertNotIn("INSERT INTO role_binding", sql)

    def test_main_parses_args_when_not_initialized(self):
        self.script.args = None
        parsed = self._args(dry_run=True)
        with patch.object(self.script.parser, "parse_args", return_value=parsed), patch.object(
            self.script,
            "resolve_customer_uuid",
            return_value=("11111111-2222-3333-4444-555555555555", "ok"),
        ), patch.object(
            self.script,
            "fetch_single_value",
            side_effect=["0", "0", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"],
        ), patch("builtins.print"):
            self.script.main()
