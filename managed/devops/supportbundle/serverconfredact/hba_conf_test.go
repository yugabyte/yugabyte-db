// Copyright (c) YugabyteDB, Inc.

package serverconfredact

import (
	"bufio"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"hash"
	"os"
	"regexp"
	"slices"
	"strings"
	"testing"
)

// Shared with HbaConfRedactorTest.java.
const (
	hbaGoldenRulesPath = "../../../src/test/resources/redaction/hba_pg_rules.jsonl"
	hbaParityPath      = "../../../src/test/resources/redaction/hba_redactor_parity.txt"
)

const ldapRule = "host all all 0.0.0.0/0 ldap ldapserver=h ldapbasedn=x "

// The cases of HbaConfRedactorTest.java.
func TestRedactHbaConfCSV(t *testing.T) {
	const rest = ",local all all trust"
	cases := []struct{ want, in string }{
		{ldapRule + `ldapbindpasswd=REDACTED ldaptls=1`, ldapRule + `ldapbindpasswd=secret ldaptls=1`},
		{`"` + ldapRule + `ldapbindpasswd=REDACTED",local all all trust`,
			`"` + ldapRule + `ldapbindpasswd=secret",local all all trust`},
		{`"` + ldapRule + `ldapserver=h,ldapbindpasswd=REDACTED,ldaptls=1"`,
			`"` + ldapRule + `ldapserver=h,ldapbindpasswd=secret,ldaptls=1"`},
		{`"` + ldapRule + `ldapbindpasswd=""REDACTED"" ldaptls=1"`,
			`"` + ldapRule + `ldapbindpasswd=""pa ss, #x"" ldaptls=1"`},
		{ldapRule + `ldapbindpasswd="REDACTED",local all all trust`,
			ldapRule + `ldapbindpasswd="pa ss",local all all trust`},
		{`"` + ldapRule + `ldapbindpasswd=""REDACTED"""`, `"` + ldapRule + `ldapbindpasswd=""a""""b"""`},
		{ldapRule + `ldapbindpasswd="REDACTED" ldaptls=1`, ldapRule + `ldapbindpasswd="" ldaptls=1`},
		{`"` + ldapRule + `ldapbindpasswd=""REDACTED"""  ,local all all trust`,
			`"` + ldapRule + `ldapbindpasswd=""pa ss"""  ,local all all trust`},
		{ldapRule + `ldapbindpasswd=""REDACTED"" ldaptls=1`, ldapRule + `ldapbindpasswd=""secret"" ldaptls=1`},
		{ldapRule + `ldapbindpasswd="REDACTED"`, ldapRule + `ldapbindpasswd=""`},
		{ldapRule + `ldapbindpasswd=REDACTED,REDACTED, ,REDACTED,local all all trust,host all all ::1/128 trust`,
			ldapRule + `ldapbindpasswd=pa,ss word, ,# x,local all all trust,host all all ::1/128 trust`},
		{ldapRule + `ldapbindpasswd=REDACTED ldaptls=1,more`, ldapRule + `ldapbindpasswd=secret ldaptls=1,more`},
		{ldapRule + `ldapbindpasswd=REDACTED ldaptls=1`, ldapRule + `ldapbindpasswd=pa"s s"word ldaptls=1`},
		{ldapRule + `"ldapbindpasswd=REDACTED"`, ldapRule + `"ldapbindpasswd=pa ss"`},
		{ldapRule + `"ldapbindpasswd=REDACTED"`, ldapRule + `"ldapbindpasswd=pa"ss`},
		{ldapRule + `ldapbindpasswd="REDACTED"`, ldapRule + `ldapbindpasswd="pa ss ldaptls=1`},
		{ldapRule + `"ldapbindpasswd"=REDACTED`, ldapRule + `"ldapbindpasswd"=secret`},
		{ldapRule + `ldap"bind"passwd=REDACTED`, ldapRule + `ldap"bind"passwd=secret`},
		{ldapRule + `ldapbindpasswd=REDACTED ldapbindpasswd=REDACTED,` + ldapRule +
			`ldapbindpasswd=REDACTED,local all all trust`,
			ldapRule + `ldapbindpasswd=one ldapbindpasswd=two,` + ldapRule +
				`ldapbindpasswd=three,local all all trust`},
		{ldapRule + `ldapbindpasswd=REDACTED` + rest, ldapRule + `ldapbindpasswd=Two Word Secret ldaptls=1` + rest},
		{ldapRule + `ldapbindpasswd=REDACTED` + rest, ldapRule + `ldapbindpasswd=secret key=abc` + rest},
		{ldapRule + `ldapbindpasswd=REDACTED` + rest, ldapRule + `ldapbindpasswd= secret` + rest},
		{`"` + ldapRule + `ldapbindpasswd=REDACTED"` + rest, `"` + ldapRule + `ldapbindpasswd=pa,ss ldaptls=1"` + rest},
		{ldapRule + `LdapBindPasswd=REDACTED` + rest, ldapRule + `LdapBindPasswd=secret` + rest},
		{ldapRule + `ldapbindpasswd=REDACTED#REDACTED`, ldapRule + `ldapbindpasswd=pa#ss`},
		{ldapRule + `ldapbindpasswd=REDACTED ldaptls=1 #REDACTED`, ldapRule + `ldapbindpasswd=pa ldaptls=1 # x`},
		{ldapRule + `ldapbindpasswd=REDACTED#REDACTED,REDACTED`, ldapRule + `ldapbindpasswd=p#a,ss`},
		{`local all all trust # ldapbindpasswd=REDACTED,local all all md5`,
			`local all all trust # ldapbindpasswd=secret,local all all md5`},
		{ldapRule + `"ldapbindpasswd=REDACTED"`, ldapRule + `"ldapbindpasswd=pa#ss"`},
		{`local all yugabyte trust, "host all all 0.0.0.0/0 ldap ldapbindpasswd=REDACTED`,
			`local all yugabyte trust, "host all all 0.0.0.0/0 ldap ldapbindpasswd=x"`},
		{ldapRule + `ldapsearchfilter="(cn=ldapbindpasswd=REDACTED`,
			ldapRule + `ldapsearchfilter="(cn=ldapbindpasswd=x)" ldapbindpasswd=secret`},
		{ldapRule + `"ldapsearchfilter=(x) ldapbindpasswd=REDACTED`,
			ldapRule + `"ldapsearchfilter=(x) ldapbindpasswd=secret"`},
		{`"` + ldapRule + `ldapbindpasswd=REDACTED`, `"` + ldapRule + `ldapbindpasswd=secret ldaptls=1`},
		{ldapRule + `ldapbindpasswd=REDACTED`, ldapRule + "ldapbindpasswd=\"pa\nss\",local all all trust"},
		{ldapRule + `\,ldapbindpasswd=REDACTED ldaptls=1`, ldapRule + `\,ldapbindpasswd=secret ldaptls=1`},
		{ldapRule + `ldapbindpasswd=REDACTED\, ldaptls=1`, ldapRule + `ldapbindpasswd=sec\,ret ldaptls=1`},
		{ldapRule + `ldapbindpasswd=REDACTED\,`, ldapRule + `ldapbindpasswd=sec\,ret junk`},
		{ldapRule + `ldapbind\,passwd=REDACTED`, ldapRule + `ldapbind\,passwd=secret`},
		{`"` + ldapRule + `ldap\",bindpasswd=REDACTED`, `"` + ldapRule + `ldap\",bindpasswd=secret`},
		{ldapRule + `ldapbindpasswd=REDACTED`, ldapRule + `ldapbindpasswd=abc\`},
	}
	for _, c := range cases {
		got := redactHbaConfCSV(c.in)
		if got != c.want {
			t.Errorf("%q:\n got %q\nwant %q", c.in, got, c.want)
		}
		if again := redactHbaConfCSV(got); again != got {
			t.Errorf("%q: unstable, %q", c.in, again)
		}
		if !hbaConfRedacted(got) {
			t.Errorf("%q: verify failed on %q", c.in, got)
		}
	}
	const plain = "host all all 0.0.0.0/0 md5,local all yugabyte trust"
	if got := redactHbaConfCSV(plain); got != plain {
		t.Errorf("changed %q", got)
	}
}

func TestRedactLdapBindPasswdText(t *testing.T) {
	cases := []struct{ want, in string }{
		{"x ldapbindpasswd=REDACTED\nnext line", "x ldapbindpasswd=secret ldaptls=1\nnext line"},
		{`{\"ysql_hba_conf_csv\":\"host ldap ldapbindpasswd=REDACTED`,
			`{\"ysql_hba_conf_csv\":\"host ldap ldapbindpasswd=\"pa ss\" ldaptls=1\"}`},
		{`ldap\"bind\"passwd=REDACTED`, `ldap\"bind\"passwd=x`},
		{`ldapbind\,passwd=REDACTED`, `ldapbind\,passwd=x`},
		{"--ysql_hba_conf_csv=" + ldapRule + "ldapbindpasswd=REDACTED ldaptls=1,local all all trust\r\n" +
			"--max_log_size=256",
			"--ysql_hba_conf_csv=" + ldapRule + "ldapbindpasswd=secret ldaptls=1,local all all trust\r\n" +
				"--max_log_size=256"},
		{"  gflags:\n    ysql_hba_conf_csv: " + ldapRule + "ldapbindpasswd=REDACTED\n      REDACTED\n" +
			"    max_log_size: 256\n",
			"  gflags:\n    ysql_hba_conf_csv: " + ldapRule + "ldapbindpasswd=\"long pass\n" +
				"      phrase tail\"\n    max_log_size: 256\n"},
		// Java stack frames are indented with a tab, not spaces.
		{"error: ldapbindpasswd=REDACTED\n\tat com.yugabyte.Foo", "error: ldapbindpasswd=x\n\tat com.yugabyte.Foo"},
		// A gflag line folds when its redaction reaches the end of the line, and only then.
		{"--ysql_hba_conf_csv=" + ldapRule + "ldapbindpasswd=REDACTED\n      REDACTED\n--max_log_size=256",
			"--ysql_hba_conf_csv=" + ldapRule + "ldapbindpasswd=pa\n      ss-tail\n--max_log_size=256"},
		{"--ysql_hba_conf_csv=\"" + ldapRule + "ldapbindpasswd=REDACTED\"\n  REDACTED",
			"--ysql_hba_conf_csv=\"" + ldapRule + "ldapbindpasswd=pa\"\n  ss-tail"},
		{"--ysql_hba_conf_csv=" + ldapRule + "ldapbindpasswd=REDACTED ldaptls=1\n  next",
			"--ysql_hba_conf_csv=" + ldapRule + "ldapbindpasswd=pa ldaptls=1\n  next"},
	}
	for _, c := range cases {
		got := redactLdapBindPasswdText(c.in)
		if got != c.want {
			t.Errorf("%q:\n got %q\nwant %q", c.in, got, c.want)
		}
		if err := verifyLdapBindPasswd(got); err != nil {
			t.Errorf("%q: %v", got, err)
		}
	}
}

func TestLdapBindPasswdValuesRedacted(t *testing.T) {
	cases := []struct {
		in string
		ok bool
	}{
		{"ldapbindpasswd=REDACTED ldaptls=1", true},
		{`"ldapbindpasswd"=""REDACTED""`, true},
		{`LdapBindPasswd =\,REDACTED`, true},
		{`ldapbind\,passwd=REDACTED`, true},
		{`"ldapbind\","passwd=REDACTED"`, true},
		{"ldapbindpasswd in a comment", true},
		// Inside CSV quotes, a comma doesn't end the field, so this is not the name.
		{`"ldapbind\,passwd=="`, true},
		{"x LDAPBINDPASSWD=secret", false},
		{`ldap"bind"passwd= "secret"`, false},
		{"ldapbindpasswd=REDACTED ldapbindpasswd=secret", false},
		{`"ldapbindpasswd"=secret`, false},
		{`ldapbind\,passwd=secret`, false},
		{`"ldapbind\","passwd=secret"`, false},
	}
	for _, c := range cases {
		if got := ldapBindPasswdValuesRedacted(c.in); got != c.ok {
			t.Errorf("%q: got %v, want %v", c.in, got, c.ok)
		}
	}
}

type hbaGoldenRule struct {
	line, status string
	passwd       *string
}

// readHbaGoldenRules reads hba_pg_rules.jsonl: [line, "valid" | "invalid" | "none" (no rule, e.g. a
// comment), the ldapbindpasswd YB's Postgres read from the line].
func readHbaGoldenRules(t *testing.T) []hbaGoldenRule {
	t.Helper()
	f, err := os.Open(hbaGoldenRulesPath)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	var rules []hbaGoldenRule
	scanner := bufio.NewScanner(f)
	scanner.Buffer(nil, 1<<20)
	for scanner.Scan() {
		if scanner.Text() == "" {
			continue
		}
		var row []*string
		if err := json.Unmarshal(scanner.Bytes(), &row); err != nil || len(row) != 3 {
			t.Fatalf("bad row %q: %v", scanner.Text(), err)
		}
		rules = append(rules, hbaGoldenRule{*row[0], *row[1], row[2]})
	}
	if err := scanner.Err(); err != nil {
		t.Fatal(err)
	}
	if len(rules) < 900 {
		t.Fatalf("read only %d rules", len(rules))
	}
	return rules
}

// csvQuoted is line as one quoted CSV field.
func csvQuoted(line string) string {
	return `"` + strings.ReplaceAll(line, `"`, `""`) + `"`
}

// bareCSVField reports whether line is also one CSV field unquoted.
func bareCSVField(line string) bool {
	return !strings.Contains(line, ",") && !strings.HasPrefix(line, `"`)
}

// Independent of the redactor: ldapbindpasswd in any case with quotes inside, then blanks and an
// '=' if there is one.
var hbaKeptKey = regexp.MustCompile(`(?i)l"*d"*a"*p"*b"*i"*n"*d"*p"*a"*s"*s"*w"*d(?:[ \t\r]*=)?`)

// Checks the redactor against how YB's Postgres parses each golden line, as
// HbaConfRedactorTest.testAgreesWithPostgres does.
func TestRedactHbaConfAgreesWithPostgres(t *testing.T) {
	for _, r := range readHbaGoldenRules(t) {
		checkHbaRuleAgainstPostgres(t, r, csvQuoted(r.line))
		if bareCSVField(r.line) {
			checkHbaRuleAgainstPostgres(t, r, r.line)
		}
	}
}

func checkHbaRuleAgainstPostgres(t *testing.T, r hbaGoldenRule, value string) {
	t.Helper()
	fields := splitCSVFields(redactHbaConfCSV(value))
	if len(fields) != 1 {
		t.Errorf("%q: %d fields", r.line, len(fields))
		return
	}
	redacted := fields[0].text
	tokens := tokenizeHba(r.line)
	if r.status == "valid" && r.passwd != nil {
		var last *string
		var want []string
		for _, token := range tokens.tokens {
			text := token.text
			if strings.HasPrefix(text, hbaOptionPrefix) {
				v := text[len(hbaOptionPrefix):]
				last = &v
				text = hbaOptionPrefix + SecretReplacement
			}
			want = append(want, text)
		}
		if last == nil || *last != *r.passwd {
			t.Errorf("%q: tokenized a different password", r.line)
		}
		actual := tokenizeHba(redacted)
		var got []string
		for _, token := range actual.tokens {
			got = append(got, token.text)
		}
		if !slices.Equal(got, want) {
			t.Errorf("%q: got tokens %q, want %q", r.line, got, want)
		}
		if tokens.commentStart >= 0 &&
			(actual.commentStart < 0 || redacted[actual.commentStart:] != "#"+SecretReplacement) {
			t.Errorf("%q: comment not redacted in %q", r.line, redacted)
		}
		return
	}
	m := hbaKeptKey.FindStringIndex(r.line)
	if m == nil {
		t.Errorf("%q: no ldapbindpasswd", r.line)
		return
	}
	if want := r.line[:m[1]] + SecretReplacement; redacted != want {
		t.Errorf("%q: got %q, want %q", r.line, redacted, want)
	}
}

// The redactor's output must pass the check, or the node drops server.conf.
func TestRedactedHbaConfPassesVerify(t *testing.T) {
	check := func(in string) {
		if out := redactHbaConfCSV(in); !hbaConfRedacted(out) {
			t.Errorf("%q: verify failed on %q", in, out)
		}
		if out := redactLdapBindPasswdText(in); verifyLdapBindPasswd(out) != nil {
			t.Errorf("%q: verify failed on %q", in, out)
		}
		conf := hbaGflagLinePrefix + in
		if out := RedactSensitiveInfo(conf, nil); VerifyRedacted(out, nil) != nil {
			t.Errorf("%q: verify failed on %q", conf, out)
		}
	}
	for _, r := range readHbaGoldenRules(t) {
		check(csvQuoted(r.line))
		check(r.line)
	}
	rng := splitMix64(hbaParitySeed)
	for c := 0; c < hbaParityCases; c++ {
		check(hbaParityCase(&rng))
	}
}

// Generated cases, made the same way by HbaConfRedactorTest.java.
const (
	hbaParitySeed  = 20261113
	hbaParityCases = 20000
)

var hbaParityKeys = []string{
	"ldapbindpasswd=", "ldapbindpasswd=", "ldapbindpasswd", "LdapBindPasswd=",
	"\"ldapbindpasswd\"=", "ldap\"bind\"passwd=", "ldapbind\\,passwd=", "ldapbind\\\"passwd=",
	"ldapb\u0130ndpasswd=", "ldapbindpasswd =", "ldapbindpasswd=\"\"",
}

var hbaParityPieces = []string{
	" ", "  ", "\t", ",", "\"", "\"\"", "#", "\\", "=", "\r", "\n", "\n  ",
	"\n--ysql_hba_conf_csv=", "--ysql_hba_conf_csv=", "host", "local", "all", "0.0.0.0/0", "ldap",
	"trust", "pw", "p w", "x=y", "ldaptls=1", "ldapserver=h", "map=m", "clientcert=verify-full",
	"ldapsearchfilter=(cn=x)", "radiussecrets=r", "\u00e9",
}

// splitMix64 is a generator simple enough to write the same in Java.
type splitMix64 uint64

func (s *splitMix64) next() uint64 {
	*s += 0x9E3779B97F4A7C15
	z := uint64(*s)
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
	z = (z ^ (z >> 27)) * 0x94D049BB133111EB
	return z ^ (z >> 31)
}

func (s *splitMix64) pick(n int) int {
	return int(s.next() % uint64(n))
}

func hbaParityCase(rng *splitMix64) string {
	var b strings.Builder
	for k := rng.pick(4); k > 0; k-- {
		b.WriteString(hbaParityPieces[rng.pick(len(hbaParityPieces))])
	}
	b.WriteString(hbaParityKeys[rng.pick(len(hbaParityKeys))])
	for k := rng.pick(8); k > 0; k-- {
		b.WriteString(hbaParityPieces[rng.pick(len(hbaParityPieces))])
	}
	return b.String()
}

func writeHbaParityOutput(h hash.Hash, s string) {
	h.Write([]byte(s))
	h.Write([]byte{0})
}

// The output must be HbaConfRedactor's, byte for byte. hba_redactor_parity.txt holds digests of
// its output over the golden lines and the generated cases, which HbaConfRedactorTest.java checks
// too; after changing either redactor, port the change and update the digests in the file.
func TestRedactHbaConfMatchesJava(t *testing.T) {
	golden := sha256.New()
	for _, r := range readHbaGoldenRules(t) {
		writeHbaParityOutput(golden, redactHbaConfCSV(csvQuoted(r.line)))
		if bareCSVField(r.line) {
			writeHbaParityOutput(golden, redactHbaConfCSV(r.line))
		}
		writeHbaParityOutput(golden, redactLdapBindPasswdText(r.line))
	}
	generated := sha256.New()
	rng := splitMix64(hbaParitySeed)
	for c := 0; c < hbaParityCases; c++ {
		value := hbaParityCase(&rng)
		writeHbaParityOutput(generated, redactHbaConfCSV(value))
		writeHbaParityOutput(generated, redactLdapBindPasswdText(value))
	}
	want := readHbaParityDigests(t)
	for name, h := range map[string]hash.Hash{"golden": golden, "generated": generated} {
		if got := hex.EncodeToString(h.Sum(nil)); got != want[name] {
			t.Errorf("%s digest %s, want %s", name, got, want[name])
		}
	}
}

func readHbaParityDigests(t *testing.T) map[string]string {
	t.Helper()
	content, err := os.ReadFile(hbaParityPath)
	if err != nil {
		t.Fatal(err)
	}
	digests := map[string]string{}
	for _, line := range strings.Split(string(content), "\n") {
		if name, digest, ok := strings.Cut(line, " "); ok && !strings.HasPrefix(line, "#") {
			digests[name] = digest
		}
	}
	return digests
}
