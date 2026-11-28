#!/usr/bin/env python3
"""Regression test: the callhome POST must not block without a timeout.

The yugabyted monitor loop joins the 'start' callhome thread with a 120 s
timeout, so a POST that hangs on an unreachable host stalls the loop and
delays restarting a crashed child process.
"""
import importlib.machinery
import importlib.util
import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))


def _load_yugabyted():
    path = os.path.join(_HERE, "yugabyted")
    loader = importlib.machinery.SourceFileLoader("yugabyted_under_test", path)
    spec = importlib.util.spec_from_loader("yugabyted_under_test", loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class _Configs(object):
    saved_data = {"callhome": True}


class _Payload(object):
    command = "start"

    def to_json(self):
        return "{}"


class CallhomeTimeoutTest(unittest.TestCase):
    def test_callhome_urlopen_has_timeout(self):
        mod = _load_yugabyted()
        seen = []

        def fake_urlopen(req, *args, **kwargs):
            seen.append(kwargs.get("timeout"))
            raise OSError("simulated unreachable host")

        mod.urlopen = fake_urlopen
        mod.Diagnostics(_Configs()).callhome(_Payload())

        self.assertEqual(len(seen), 1, "expected one callhome POST")
        self.assertIsNotNone(seen[0], "urlopen called without timeout")


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(CallhomeTimeoutTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
