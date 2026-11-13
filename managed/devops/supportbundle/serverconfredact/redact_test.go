// Copyright (c) YugabyteDB, Inc.

package serverconfredact_test

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/devops/supportbundle/serverconfredact"
)

func TestRedactConfFormat(t *testing.T) {
	input := "ycql_ldap_bind_passwd=confpass123\nldapbindpasswd=confpass456"
	out := serverconfredact.RedactSensitiveInfo(input, nil)
	if strings.Contains(out, "confpass123") || strings.Contains(out, "confpass456") {
		t.Fatalf("secrets not redacted: %q", out)
	}
	if !strings.Contains(out, serverconfredact.SecretReplacement) {
		t.Fatalf("expected REDACTED in %q", out)
	}
}

func TestRedactQuotedFormat(t *testing.T) {
	input := "ycql_ldap_bind_passwd=\"quotedpass123\"\nldapbindpasswd=\"quotedpass456\""
	out := serverconfredact.RedactSensitiveInfo(input, nil)
	if strings.Contains(out, "quotedpass123") || strings.Contains(out, "quotedpass456") {
		t.Fatalf("secrets not redacted: %q", out)
	}
}

func TestRedactCliFlagFormat(t *testing.T) {
	input := "--ycql_ldap_bind_passwd, secret456 --some_other_flag, 123"
	out := serverconfredact.RedactSensitiveInfo(input, nil)
	if strings.Contains(out, "secret456") {
		t.Fatalf("secret not redacted: %q", out)
	}
	if !strings.Contains(out, "--ycql_ldap_bind_passwd, REDACTED") {
		t.Fatalf("expected CLI redaction in %q", out)
	}
	if !strings.Contains(out, "--some_other_flag, 123") {
		t.Fatalf("other flag altered: %q", out)
	}
}

func TestRedactCommaSeparatedGflags(t *testing.T) {
	input := "rpc_throttle_threshold_bytes=1048569, ycql_ldap_bind_passwd=h*Y(^979hAB%$|~\"\"abc\"\"`P-=/.,;, webserver_port=9000"
	out := serverconfredact.RedactSensitiveInfo(input, nil)
	if strings.Contains(out, "979hAB") {
		t.Fatalf("password not fully redacted: %q", out)
	}
	if !strings.Contains(out, "ycql_ldap_bind_passwd=REDACTED") {
		t.Fatalf("expected gflag redaction in %q", out)
	}
	if !strings.Contains(out, "webserver_port=9000") {
		t.Fatalf("webserver_port altered: %q", out)
	}
}

func TestRedactHBADoubledCSV(t *testing.T) {
	input := "\"host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com ldapport=389 " +
		"ldapbinddn=\"\"cn=admin,dc=example,dc=com\"\" " +
		"ldapbindpasswd=MyPassword123 " +
		"ldapbasedn=\"\"dc=example,dc=com\"\" ldapsearchattribute=uid\""
	out := serverconfredact.RedactSensitiveInfo(input, nil)
	if strings.Contains(out, "MyPassword123") {
		t.Fatalf("password not redacted: %q", out)
	}
	if !strings.Contains(out, "ldapbasedn=\"\"dc=example,dc=com\"\"") {
		t.Fatalf("ldapbasedn eaten: %q", out)
	}
}

func TestRedactSensitiveGflagFromList(t *testing.T) {
	input := "--custom_sensitive_flag=topsecret\n--webserver_port=13000"
	out := serverconfredact.RedactSensitiveInfo(input, []string{"custom_sensitive_flag"})
	if strings.Contains(out, "topsecret") {
		t.Fatalf("custom gflag not redacted: %q", out)
	}
	if !strings.Contains(out, "webserver_port=13000") {
		t.Fatalf("non-sensitive flag altered: %q", out)
	}
}

func TestYsqlHbaConfCSVExcludedFromWholesale(t *testing.T) {
	input := "ysql_hba_conf_csv=host all all 0.0.0.0/0 ldap ldapbindpasswd=secret123"
	out := serverconfredact.RedactSensitiveInfo(input, []string{"ysql_hba_conf_csv"})
	if strings.Contains(out, "secret123") {
		t.Fatalf("ldapbindpasswd inside hba should still redact via default pattern: got %q", out)
	}
}

// A gflag value whose closing delimiter is missing must still redact and must not panic.
//
// Truncation is routine in the inputs this runs on: the redactor is pointed at log excerpts and at
// files a process is still writing. Each scanner advanced past the closing delimiter unconditionally,
// so an absent one left the read cursor beyond end-of-input and the next pass sliced out of bounds.
// A panic means a non-zero exit, which the collector reads as REDACTION_STATUS=SKIPPED -- so the
// cost of this was a bundle that fell back to the YBA pass for the whole file.
func TestRedactUnterminatedValue(t *testing.T) {
	const field = "ycql_ldap_bind_passwd"
	cases := []struct {
		name   string
		input  string
		secret string
	}{
		{"single_quoted_cli", `['--` + field + `', 'sqsecret`, "sqsecret"},
		{"esc_dbl_quoted_cli", `[\"--` + field + `\", \"edqsecret`, "edqsecret"},
		{"esc_dbl_quoted_cli_empty", `[\"--` + field + `\", \"`, ""},
		{"python_dict", `{'key': '` + field + `', 'value': 'pydictsecret`, "pydictsecret"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			out, panicked := redactNoPanic(tc.input)
			if panicked != nil {
				t.Fatalf("panic on unterminated input %q: %v", tc.input, panicked)
			}
			if tc.secret != "" && strings.Contains(out, tc.secret) {
				t.Fatalf("secret %q survived: %q", tc.secret, out)
			}
		})
	}
}

// redactNoPanic reports a panic instead of taking the test binary down with it, so one bad case
// does not hide the others.
func redactNoPanic(input string) (out string, recovered interface{}) {
	defer func() { recovered = recover() }()
	return serverconfredact.RedactSensitiveInfo(input, nil), nil
}

// Every hba quoting style must redact, including the single-escaped form that has no QuoteStyle of
// its own.
//
// This is a parity contract, not a local invariant: a file is redacted on the node by this package
// and, when that is skipped, on the YBA host by LdapBindPasswdHbaFormat. Java reaches its unquoted
// branch for anything not starting with a literal '"', so the single-escaped form redacts there as
// unquoted. Excluding a leading backslash here made the node disagree, and since a clean node run
// reports REDACTION_STATUS=NODE and suppresses the YBA pass, the disagreement shipped the password.
func TestRedactHBAAllQuoteStyles(t *testing.T) {
	const pw = "topsecret"
	cases := []struct {
		name string
		in   string
		want string
	}{
		{"unquoted", `ldapbindpasswd=` + pw + ` ldaptls=0`, `ldapbindpasswd=REDACTED`},
		{"single_csv", `ldapbindpasswd="` + pw + `" ldaptls=0`, `ldapbindpasswd="REDACTED"`},
		{"doubled_csv", `ldapbindpasswd=""` + pw + `"" ldaptls=0`, `ldapbindpasswd=""REDACTED""`},
		{"esc_doubled_csv", `ldapbindpasswd=\"\"` + pw + `\"\" ldaptls=0`, `ldapbindpasswd=\"\"REDACTED\"\"`},
		{"deep_esc_doubled_csv", `ldapbindpasswd=\\"\\"` + pw + `\\"\\" ldaptls=0`, `ldapbindpasswd=\\"\\"REDACTED\\"\\"`},
		// No quote style of its own; Java treats it as unquoted, so the wrapper is not preserved.
		{"single_escaped", `ldapbindpasswd=\"` + pw + `\" ldaptls=0`, `ldapbindpasswd=REDACTED`},
		{"single_escaped_at_eol", `ldapbindpasswd=\"` + pw + `\"`, `ldapbindpasswd=REDACTED`},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			out := serverconfredact.RedactSensitiveInfo("host all all 0.0.0.0/0 ldap "+tc.in, nil)
			if strings.Contains(out, pw) {
				t.Fatalf("password survived: %q", out)
			}
			if !strings.Contains(out, tc.want) {
				t.Fatalf("want %q in %q", tc.want, out)
			}
		})
	}
}

// radiussecrets is a comma list, one secret per radiusservers entry; PostgreSQL wants the list
// double-quoted and an element with blanks quoted again. Commas inside it must not end the value.
func TestRedactRadiusSecretsInEveryQuoteStyle(t *testing.T) {
	cases := []struct {
		name string
		in   string
		want string
	}{
		{"unquoted", `radiussecrets=topsecret radiusports=1812`,
			`radiussecrets=REDACTED radiusports=1812`},
		{"single_csv", `radiussecrets="topsecret,othersecret" radiusports=1812`,
			`radiussecrets="REDACTED" radiusports=1812`},
		{"doubled_csv", `radiussecrets=""topsecret,othersecret"" radiusports=1812`,
			`radiussecrets=""REDACTED"" radiusports=1812`},
		{"doubled_csv_quoted_elements",
			`radiussecrets=""""""top secret"""",""""othersecret"""""" radiusports=1812`,
			`radiussecrets=""REDACTED"" radiusports=1812`},
		{"esc_doubled_csv", `radiussecrets=\"\"topsecret,othersecret\"\" radiusports=1812`,
			`radiussecrets=\"\"REDACTED\"\" radiusports=1812`},
		{"deep_esc_doubled_csv", `radiussecrets=\\"\\"topsecret,othersecret\\"\\" radiusports=1812`,
			`radiussecrets=\\"\\"REDACTED\\"\\" radiusports=1812`},
		{"before_next_record", `radiussecrets=""topsecret,othersecret""",local all yugabyte trust`,
			`radiussecrets=""REDACTED""",local all yugabyte trust`},
	}
	sensitive := []string{"ysql_hba_conf_csv"}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			in := `--ysql_hba_conf_csv="host all all 0.0.0.0/0 radius radiusservers=""r1,r2"" ` +
				tc.in
			out := serverconfredact.RedactSensitiveInfo(in, sensitive)
			if strings.Contains(out, "topsecret") || strings.Contains(out, "top secret") ||
				strings.Contains(out, "othersecret") {
				t.Fatalf("secret survived: %q", out)
			}
			if !strings.Contains(out, `radiusservers=""r1,r2"" `+tc.want) {
				t.Fatalf("want %q in %q", tc.want, out)
			}
			if err := serverconfredact.VerifyRedacted(out, sensitive); err != nil {
				t.Fatalf("%q: %v", out, err)
			}
		})
	}
}

func TestLdapBindPasswdValueEndsAtAnyHbaOption(t *testing.T) {
	for _, next := range []string{"clientcert=verify-full", "map=ldapmap", "include_realm=0"} {
		input := `--ysql_hba_conf_csv="hostssl all all 0.0.0.0/0 ldap ldapbindpasswd=""s3cretpw"" ` +
			next + `"`
		out := serverconfredact.RedactSensitiveInfo(input, nil)
		if strings.Contains(out, "s3cretpw") || !strings.Contains(out, `ldapbindpasswd=""REDACTED"" `+next) {
			t.Fatalf("followed by %s: %q", next, out)
		}
	}
	// pg_hba separates attributes with whitespace: a name= right against the quote is the password.
	out := serverconfredact.RedactSensitiveInfo(`ldapbindpasswd=""p""x=tail""`, nil)
	if strings.Contains(out, "x=tail") {
		t.Fatalf("value cut at an adjacent name=: %q", out)
	}
}

// gflags reads a flagfile one flag per line and takes the rest of it as the value, so a second
// "--" on the line is part of the secret.
func TestSensitiveFlagValueRunsToEndOfLine(t *testing.T) {
	cases := []struct{ input, want string }{
		{"--ycql_ldap_bind_passwd=abc,def=ghi", "--ycql_ldap_bind_passwd=REDACTED"},
		{"--ycql_ldap_bind_passwd=abc --def", "--ycql_ldap_bind_passwd=REDACTED"},
		{"--ycql_ldap_bind_passwd=abc,def=ghi --webserver_port=9000",
			"--ycql_ldap_bind_passwd=REDACTED"},
		{"  --ycql_ldap_bind_passwd=a b,cde=f\n--port=5433", "  --ycql_ldap_bind_passwd=REDACTED\n--port=5433"},
		{`--ycql_ldap_bind_passwd="abc,def=ghi"`, `--ycql_ldap_bind_passwd="REDACTED"`},
		{`--my_secret_flag=\"pw --x\"`, `--my_secret_flag=\"REDACTED\"`},
		{`--my_secret_flag="pw" --x`, "--my_secret_flag=REDACTED"},
		{`--my_secret_flag=\pw --x`, "--my_secret_flag=REDACTED"},
		{"--my_secret_flag=x,yz1=2", "--my_secret_flag=REDACTED"},
		{"ycql_ldap_bind_passwd=abc --def", "ycql_ldap_bind_passwd=REDACTED"},
	}
	for _, c := range cases {
		if out := serverconfredact.RedactSensitiveInfo(c.input, []string{"my_secret_flag"}); out != c.want {
			t.Fatalf("%q: got %q, want %q", c.input, out, c.want)
		}
	}
}

func TestVerifyRedactedFindsLeftoverSecrets(t *testing.T) {
	leftovers := []string{
		`--ysql_hba_conf_csv="host all all 0.0.0.0/0 ldap ldapbindpasswd=""leakpw"" clientcert=1"`,
		`ldapbindpasswd=leakpw`,
		`--ycql_ldap_bind_passwd=leakpw`,
		`--my_secret_flag="leakpw"`,
		"--port=1 --ycql_ldap_bind_passwd=REDACTED\n--ycql_ldap_bind_passwd=leakpw",
		`--ycql_ldap_bind_passwd=REDACTED --leakpw`,
		`--my_secret_flag="REDACTED" leakpw`,
		`radiussecrets=leakpw`,
		`--ysql_hba_conf_csv="host all all 0.0.0.0/0 radius radiussecrets=""leakpw"""`,
	}
	for _, s := range leftovers {
		err := serverconfredact.VerifyRedacted(s, []string{"my_secret_flag"})
		if err == nil || strings.Contains(err.Error(), "leakpw") {
			t.Fatalf("%q: want an error that doesn't quote the secret, got %v", s, err)
		}
	}
	clean := []string{
		`ldapbindpasswd=REDACTED ldaptls=0`,
		`ldapbindpasswd=""REDACTED"" clientcert=1`,
		`ldapbindpasswd=\"\"REDACTED\"\"`,
		`ldapbindpasswd="REDACTED"`,
		`--ysql_hba_conf_csv="host all all 0.0.0.0/0 ldap ldapbindpasswd="""`,
		`--ycql_ldap_bind_passwd=REDACTED`,
		`--ycql_ldap_bind_passwd=`,
		`radiussecrets=REDACTED radiusports=1812`,
		`radiussecrets=""REDACTED"" radiusports=1812`,
		`--my_secret_flag="REDACTED"`,
		`--webserver_port=9000`,
	}
	for _, s := range clean {
		if err := serverconfredact.VerifyRedacted(s, []string{"my_secret_flag"}); err != nil {
			t.Fatalf("%q: %v", s, err)
		}
	}
}

func TestRedactedOutputPassesVerify(t *testing.T) {
	inputs := []string{
		`--ysql_hba_conf_csv="hostssl all all 0.0.0.0/0 ldap ldapbindpasswd=""pw"" clientcert=verify-full"`,
		`--ysql_hba_conf_csv="host all all 0.0.0.0/0 ldap ldapbindpasswd=pw ldapport=389"`,
		"--ycql_ldap_bind_passwd=abc,def=ghi\n--my_secret_flag=a,bcd=e --port=5433",
		`--ycql_ldap_bind_passwd="abc,def=ghi"`,
		"ycql_ldap_bind_passwd=confpass123\nldapbindpasswd=confpass456",
		"--ycql_ldap_bind_passwd=abc --def",
		`--ysql_hba_conf_csv="host all all 0.0.0.0/0 radius radiussecrets=""s1,s2"""`,
	}
	for _, in := range inputs {
		sensitive := []string{"my_secret_flag"}
		out := serverconfredact.RedactSensitiveInfo(in, sensitive)
		if err := serverconfredact.VerifyRedacted(out, sensitive); err != nil {
			t.Fatalf("%q -> %q: %v", in, out, err)
		}
	}
}

func TestWriteIfRedactedLeavesASecretUnwritten(t *testing.T) {
	path := filepath.Join(t.TempDir(), "server.conf")
	original := "--ycql_ldap_bind_passwd=s3cret\n"
	if err := os.WriteFile(path, []byte(original), 0o600); err != nil {
		t.Fatal(err)
	}
	// What a redactor that missed the value would hand over: not written, ErrUnredacted for exit 3.
	err := serverconfredact.WriteIfRedacted(path, original, nil)
	if !errors.Is(err, serverconfredact.ErrUnredacted) {
		t.Fatalf("got %v, want ErrUnredacted", err)
	}
	if got, _ := os.ReadFile(path); string(got) != original {
		t.Fatalf("file changed: %q", got)
	}
	redacted := serverconfredact.RedactSensitiveInfo(original, nil)
	if err := serverconfredact.WriteIfRedacted(path, redacted, nil); err != nil {
		t.Fatal(err)
	}
	if got, _ := os.ReadFile(path); string(got) != "--ycql_ldap_bind_passwd=REDACTED\n" {
		t.Fatalf("got %q", got)
	}
}
