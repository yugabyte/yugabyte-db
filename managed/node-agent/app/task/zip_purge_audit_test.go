// Copyright (c) YugabyteDB, Inc.

package task

import (
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

// zip_purge_yb_logs.sh archives audit records out of the plain logs before
// gzipping them, so a truncated archive means retained audit data is lost. The
// records are multi-line (a pgaudit / YCQL statement can contain newlines), so
// this test guards that archive_audit_lines() captures whole records and not
// just their first physical line (PLAT-22410).

const auditTemplatePath = "../../resources/templates/server/zip_purge_yb_logs.sh.j2"

// extractAuditShellFns pulls the audit-archive pieces out of the template so the
// test exercises the real script logic rather than a copy that could drift. The
// slice has no Jinja in it, so it is valid bash as-is.
func extractAuditShellFns(t *testing.T) string {
	t.Helper()
	raw, err := os.ReadFile(auditTemplatePath)
	if err != nil {
		t.Fatalf("read template: %v", err)
	}
	body := string(raw)

	var b strings.Builder
	for _, line := range strings.Split(body, "\n") {
		if strings.HasPrefix(line, "ysql_audit_line_start_regex=") ||
			strings.HasPrefix(line, "readonly YSQL_AUDIT_PATTERN=") ||
			strings.HasPrefix(line, "readonly YCQL_AUDIT_PATTERN=") ||
			strings.HasPrefix(line, "readonly YCQL_AUDIT_START_RE=") {
			b.WriteString(line + "\n")
		}
	}

	for _, name := range []string{
		`\narchive_audit_lines\(\) \{.*?\n\}\n`,
		`\narchive_postgres_audit_lines\(\) \{.*?\n\}\n`,
		`\narchive_ycql_audit_log\(\) \{.*?\n\}\n`,
	} {
		m := regexp.MustCompile(`(?s)` + name).FindString(body)
		if m == "" {
			t.Fatalf("could not extract %q from template", name)
		}
		b.WriteString(m)
	}
	return b.String()
}

func runAuditArchive(t *testing.T, fixture, wrapper, srcName string) string {
	t.Helper()
	return runAuditArchiveWithRegex(t, fixture, wrapper, srcName, "")
}

// ysqlStartRe overrides the record-start pattern the template falls back to, standing in for the
// one YBA generates from log_line_prefix and ships in log_cleanup_env. Empty keeps the default.
func runAuditArchiveWithRegex(
	t *testing.T, fixture, wrapper, srcName, ysqlStartRe string) string {
	t.Helper()
	dir := t.TempDir()
	src := filepath.Join(dir, srcName)
	if err := os.WriteFile(src, []byte(fixture), 0o644); err != nil {
		t.Fatalf("write fixture: %v", err)
	}
	auditDir := filepath.Join(dir, "audit")

	override := ""
	if ysqlStartRe != "" {
		// After the extracted default assignment, so it wins; the functions read it when called.
		override = "ysql_audit_line_start_regex='" + ysqlStartRe + "'\n"
	}
	script := "set -euo pipefail\nlog_purge_failures=0\n" +
		extractAuditShellFns(t) + override +
		"\n" + wrapper + " \"$1\" \"$2\"\n"

	cmd := exec.Command("bash", "-c", script, "bash", src, auditDir)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("archive run failed: %v\n%s", err, out)
	}

	gz := filepath.Join(auditDir, srcName+".audit.log.gz")
	if _, err := os.Stat(gz); err != nil {
		return "" // no archive produced (no audit records)
	}
	out, err := exec.Command("gzip", "-dc", gz).Output()
	if err != nil {
		t.Fatalf("gunzip archive: %v", err)
	}
	return string(out)
}

func TestArchiveKeepsMultiLineYsqlAuditRecords(t *testing.T) {
	fixture := strings.Join([]string{
		`2024-04-19 15:26:08.973 UTC [10011] LOG:  AUDIT: SESSION,1,1,DDL,CREATE TABLE,,,"CREATE TABLE t1(id int);",<none>`,
		`2024-04-19 15:26:09.001 UTC [10011] LOG:  AUDIT: SESSION,2,1,WRITE,INSERT,,,"INSERT INTO t1`,
		`VALUES (1),`,
		`(2);",<none>`,
		`2024-04-19 15:26:09.100 UTC [10011] LOG:  statement: SELECT 1`,
		`continuation line mentioning LOG:  AUDIT: but not a real record start`,
		"",
	}, "\n")

	got := runAuditArchive(t, fixture, "archive_postgres_audit_lines", "postgresql-1.log")

	// Both audit records in full, including the two continuation lines of record 2.
	for _, want := range []string{
		`"CREATE TABLE t1(id int);"`,
		"INSERT INTO t1\nVALUES (1),\n(2);",
	} {
		if !strings.Contains(got, want) {
			t.Errorf("archive missing %q\ngot:\n%s", want, got)
		}
	}
	// The query record and the decoy continuation must not be archived.
	if strings.Contains(got, "statement: SELECT 1") ||
		strings.Contains(got, "not a real record start") {
		t.Errorf("archive wrongly included non-audit content:\n%s", got)
	}
}

func TestArchiveKeepsMultiLineYcqlAuditRecords(t *testing.T) {
	fixture := strings.Join([]string{
		`I0419 15:26:08.111111 12345 audit_logger.cc:1] AUDIT: user:cassandra|host:10.0.0.1|type:QUERY|operation:SELECT * FROM t`,
		`WHERE id = 1;`,
		`I0419 15:26:08.222222 12345 tablet.cc:2] some other glog line`,
		"",
	}, "\n")

	got := runAuditArchive(t, fixture, "archive_ycql_audit_log", "yb-tserver.INFO.log")

	if !strings.Contains(got, "operation:SELECT * FROM t\nWHERE id = 1;") {
		t.Errorf("YCQL multi-line record truncated\ngot:\n%s", got)
	}
	if strings.Contains(got, "some other glog line") {
		t.Errorf("archive wrongly included non-audit glog line:\n%s", got)
	}
}

func TestArchiveNoAuditRecordsProducesNoArchive(t *testing.T) {
	fixture := "2024-04-19 15:26:09.100 UTC [10011] LOG:  statement: SELECT 1\n"
	if got := runAuditArchive(t, fixture, "archive_postgres_audit_lines", "postgresql-2.log"); got != "" {
		t.Errorf("expected no archive for a log with no audit records, got:\n%s", got)
	}
}

// The pattern a node actually gets is generated by YBA from log_line_prefix rather than the
// template's default, and OtelCollectorConfigGeneratorTest pins the exact string for this prefix
// ("%t [%p] %u@%d "). It is POSIX ERE for awk, so evaluate it where it runs: java.util.regex
// would reject the portable "[[]" spelling outright and tells us nothing about mawk or gawk.
func TestArchiveKeepsMultiLineYsqlAuditRecordsForACustomLogLinePrefix(t *testing.T) {
	const customPrefixEre = `^([A-Z][0-9]+)|^(([0-9]+-[0-9]+-[0-9]+ [0-9]+:[0-9]+:[0-9]+ ` +
		`[A-Za-z0-9_]+)[ ][[]([0-9]+)[]][ ]([^@]+)[@]([^ ]+)[ ])`

	fixture := strings.Join([]string{
		`2024-04-19 15:26:08 UTC [10011] alice@db1 LOG:  AUDIT: SESSION,1,1,WRITE,INSERT,,,"INSERT INTO t1`,
		`VALUES (1),`,
		`(2);",<none>`,
		`2024-04-19 15:26:09 UTC [10011] alice@db1 LOG:  statement: SELECT 1`,
		"",
	}, "\n")

	got := runAuditArchiveWithRegex(
		t, fixture, "archive_postgres_audit_lines", "postgresql-3.log", customPrefixEre)

	if !strings.Contains(got, "INSERT INTO t1\nVALUES (1),\n(2);") {
		t.Errorf("custom-prefix multi-line record truncated\ngot:\n%s", got)
	}
	if strings.Contains(got, "statement: SELECT 1") {
		t.Errorf("archive wrongly included a non-audit record:\n%s", got)
	}
}
