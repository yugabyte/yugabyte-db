"""Fixture tests for metric_name_lint.py.

Run: python3 -m unittest discover -s <this dir> -p metric_name_lint_test.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
LINT = os.path.join(HERE, "metric_name_lint.py")

FIXTURES = {
    # ---- C++ (yugabyte-db) -------------------------------------------------------------
    "src/bad_dynamic.cc": """
        void F(const scoped_refptr<MetricEntity>& e, const std::string& svc) {
          auto id = Format("rpcs_in_queue_$0", svc);
          auto g = std::make_shared<OwningGaugePrototype<int64_t>>("server", id, "d",
              MetricUnit::kRequests, "d", MetricLevel::kInfo);
        }
    """,
    "src/bad_family.cc": """
        METRIC_DEFINE_histogram(server, handler_latency_yb_cqlserver_SQLProcessor_SelectStmt,
            "a", u, "b", 1, 2);
        METRIC_DEFINE_histogram(server, handler_latency_yb_cqlserver_SQLProcessor_InsertStmt,
            "a", u, "b", 1, 2);
        METRIC_DEFINE_histogram(server, handler_latency_yb_cqlserver_SQLProcessor_UpdateStmt,
            "a", u, "b", 1, 2);
    """,
    "src/bad_paste.h": """
        #define DEFINE_POOL_METRICS(entity, name) \\
            METRIC_DEFINE_event_stats(entity, BOOST_PP_CAT(name, _queue_length), "l", u, "d")
    """,
    "src/good.cc": """
        METRIC_DEFINE_entity(cgroup);
        METRIC_DEFINE_gauge_int64(cgroup, cgroup_cpu_usage_ns, "CPU", u, "d");
        METRIC_DEFINE_counter(tablet, rocksdb_block_cache_add, "a", u, "d");
        METRIC_DEFINE_counter(tablet, rocksdb_block_cache_add_failures, "a", u, "d");
        METRIC_DEFINE_counter(tablet, rocksdb_block_cache_bytes_read, "a", u, "d");
        #define MINIT(x) x(METRIC_##x.Instantiate(entity))
        // auto p = std::make_unique<OwningGaugePrototype<int>>("server", commented_out, ...);
        auto p = std::make_unique<OwningGaugePrototype<int>>(
            "server", "fixed_name", "l", u, "d", l);
    """,
    "src/suppressed.cc": """
        // metric-name-lint: allow(fixed table of distinct rocksdb tickers)
        auto p = std::make_unique<OwningGaugePrototype<int>>("tablet", ticker_name, "l", u, "d", l);
    """,
    "ext/pg_metrics.c": """
        #define YSQL_LATENCY_METRIC_PREFIX "handler_latency_yb_ysqlserver_SQLProcessor_"
    """,
    # ---- Go ------------------------------------------------------------------------------
    "go/bad.go": """
        package m
        var g = prometheus.NewGaugeVec(prometheus.GaugeOpts{
            Name: fmt.Sprintf("%s_queue_size", pool),
        }, []string{})
        var d = prometheus.NewDesc("yb_" + table + "_rows", "h", nil, nil)
    """,
    "go/good.go": """
        package m
        const metricName = "queue_size"
        var g = prometheus.NewGaugeVec(prometheus.GaugeOpts{
            Namespace: "billing", Name: "queue_size", Help: "h",
        }, []string{"pool"})
        var h = prometheus.NewCounterVec(prometheus.CounterOpts{Name: metricName}, nil)
    """,
    # ---- Java ----------------------------------------------------------------------------
    "java/Bad.java": """
        import io.micrometer.core.instrument.MeterRegistry;
        class Bad {
          Gauge g = Gauge.builder().name("ts_universe_" + universeType + "_status").register();
          Counter c = MetricsUtil.buildCounter("pa_task_" + taskName + "_failures", "h");
          Counter m = registry.counter("ybm." + op + ".count");
        }
    """,
    "java/Good.java": """
        class Good {
          static final String TASK_STATUS = "ts_universe_task_status";
          Gauge g = Gauge.builder().name("ts_universe_task_status").labelNames("task").register();
          Summary s = MetricsUtil.buildSummary(TASK_STATUS, "h", "universe");
          public static Summary buildSummary(String name, String description, String... labels) {
            return Summary.builder().name(name).help(description).register();
          }
        }
    """,
    # ---- Python --------------------------------------------------------------------------
    "py/bad.py": """
        from prometheus_client import Gauge
        g = Gauge(f"ybm_{job}_duration_seconds", "h")
    """,
    "java/Domain.java": """
        class Domain {
          Anomaly a = Anomaly.builder().summary("Anomaly " + i).name("x" + i).build();
        }
    """,
    "py/good.py": """
        from prometheus_client import Gauge
        g = Gauge("gcp_lb_healthy_instances", "Healthy instances", labelnames=["load_balancer"])
    """,
    # ---- Consumers (PromQL) --------------------------------------------------------------
    "conf/cluster_metrics.json": """
        {"Q": "max_over_time({__name__=~\\"rpcs_in_queue_yb_master_.*\\", export_type='x'}[5m])",
         "ALLOWLIST": "{__name__=~\\"up|kube_.*|coredns_.*\\"}",
         "GROUPED": "last_over_time({__name__=~\\"ybp_(create|schedule)_backup_status\\"}[1d])",
         "OK": "sum(rate(rpc_latency_count{service_method=\\"SelectStmt\\"}[1m]))"}
    """,
}

EXPECT_NEW = {
    ("src/bad_dynamic.cc", "DYNAMIC_NAME"),
    ("src/bad_family.cc", "NAME_FAMILY"),
    ("src/bad_paste.h", "TOKEN_PASTED_NAME"),
    ("ext/pg_metrics.c", "PREFIX_CONCAT"),
    ("go/bad.go", "DYNAMIC_NAME"),
    ("java/Bad.java", "DYNAMIC_NAME"),
    ("py/bad.py", "DYNAMIC_NAME"),
    ("conf/cluster_metrics.json", "PROMQL_NAME_REGEX"),
}


def run(root, *args):
    p = subprocess.run([sys.executable, LINT, "--root", root] + list(args),
                       capture_output=True, text=True)
    return p.returncode, p.stdout


class LintTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        for rel, body in FIXTURES.items():
            p = os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w") as f:
                f.write(textwrap.dedent(body))
        with open(os.path.join(self.root, ".metric-name-lint.json"), "w") as f:
            json.dump({"exclude": []}, f)

    def tearDown(self):
        shutil.rmtree(self.root)

    def findings(self):
        rc, out = run(self.root, "--no-baseline", "--format", "json")
        return rc, json.loads(out)["new"]

    def test_expected_findings_and_no_false_positives(self):
        rc, new = self.findings()
        got = {(f["path"], f["rule"]) for f in new}
        self.assertEqual(rc, 1)
        self.assertTrue(EXPECT_NEW <= got, "missing: %s" % (EXPECT_NEW - got))
        for good in ("src/good.cc", "go/good.go", "java/Good.java", "java/Domain.java",
                     "py/good.py",
                     "src/suppressed.cc"):
            self.assertNotIn(good, {f["path"] for f in new}, "false positive in " + good)
        # Java: all three concatenations caught.
        self.assertEqual(sum(1 for f in new if f["path"] == "java/Bad.java"), 3)
        # Go: Opts.Name and NewDesc both caught.
        self.assertEqual(sum(1 for f in new if f["path"] == "go/bad.go"), 2)

    def test_promql_is_warning_only(self):
        _, new = self.findings()
        promql = [f for f in new if f["rule"] == "PROMQL_NAME_REGEX"]
        self.assertEqual(len(promql), 2)
        self.assertFalse(promql[0]["blocking"])

    def test_ratchet(self):
        rc, _ = run(self.root, "--update-baseline")
        self.assertEqual(rc, 0)
        rc, out = run(self.root)
        self.assertEqual(rc, 0, out)                        # everything baselined
        with open(os.path.join(self.root, "src/new.cc"), "w") as f:
            f.write('auto p = std::make_unique<OwningGaugePrototype<int>>("server", '
                    'StrCat("x_", v), "l", u, "d", l);\n')
        rc, out = run(self.root)
        self.assertEqual(rc, 1, out)                        # new violation fails
        os.remove(os.path.join(self.root, "src/new.cc"))
        os.remove(os.path.join(self.root, "py/bad.py"))     # debt paid down
        rc, out = run(self.root)
        self.assertEqual(rc, 0, out)
        self.assertIn("1 stale", out)
        rc, out = run(self.root, "--fail-on-stale")
        self.assertEqual(rc, 1, out)                        # CI forces baseline to shrink

    def test_fingerprint_stable_across_line_moves(self):
        run(self.root, "--update-baseline")
        p = os.path.join(self.root, "src/bad_dynamic.cc")
        with open(p) as f:
            body = f.read()
        with open(p, "w") as f:
            f.write("\n\n\n// unrelated edit\n" + body)
        rc, out = run(self.root)
        self.assertEqual(rc, 0, out)

    def test_baseline_growth_is_blocked(self):
        def g(*a):
            return subprocess.run(["git"] + list(a), cwd=self.root, capture_output=True)
        g("init", "-q")
        g("config", "user.email", "t@t")
        g("config", "user.name", "t")
        os.remove(os.path.join(self.root, "py/bad.py"))
        g("add", "-A")
        run(self.root, "--update-baseline")
        g("add", "-A")
        g("commit", "-qm", "base")
        with open(os.path.join(self.root, "py/bad.py"), "w") as f:     # sneak debt back in
            f.write('from prometheus_client import Gauge\ng = Gauge(f"x_{y}", "h")\n')
        g("add", "-A")
        run(self.root, "--update-baseline")                            # ...and baseline it
        rc, out = run(self.root, "--no-baseline-growth", "HEAD")
        self.assertEqual(rc, 1, out)
        self.assertIn("ADDS 1", out)
        env = dict(os.environ, METRIC_NAME_LINT_ALLOW_GROWTH="1")
        p = subprocess.run([sys.executable, LINT, "--root", self.root, "--no-baseline-growth",
                            "HEAD"], capture_output=True, text=True, env=env)
        self.assertEqual(p.returncode, 0, p.stdout)

    def test_scrape_mode(self):
        dump = os.path.join(self.root, "scrape.prom")
        with open(dump, "w") as f:
            for m in ("Write", "Read", "GetTabletStatus", "ListTablets"):
                f.write('service_request_bytes_yb_tserver_TabletServerService_%s{x="1"} 1\n' % m)
            f.write("rpc_latency_count{service_method=\"Write\"} 3\n")
        rc, out = run(self.root, "--scrape", dump, "--no-baseline")
        self.assertEqual(rc, 1)
        self.assertIn("service_request_bytes_yb_tserver_TabletServerService_", out)


if __name__ == "__main__":
    unittest.main()
