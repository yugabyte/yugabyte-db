// Copyright (c) YugabyteDB, Inc.

package common

import (
	"strings"
	"testing"
)

func TestPostgresOpenSSLFipsConfig(t *testing.T) {
	cnf := PostgresOpenSSLFipsConfig("/opt/yugabyte/software/2.31/pgsql/ssl/fipsmodule.cnf")
	for _, want := range []string{
		".include /opt/yugabyte/software/2.31/pgsql/ssl/fipsmodule.cnf\n",
		"fips = fips_sect\n",
		"default_properties = fips=yes\n",
		"config_diagnostics = 1\n",
	} {
		if !strings.Contains(cnf, want) {
			t.Errorf("config is missing %q:\n%s", want, cnf)
		}
	}
}

func TestPgQuote(t *testing.T) {
	tests := []struct{ fn, in, want string }{
		{"ident", "postgres", `"postgres"`},
		{"ident", `we"ird`, `"we""ird"`},
		{"literal", "plain", `E'plain'`},
		{"literal", `it's`, `E'it''s'`},
		{"literal", `back\slash`, `E'back\\slash'`},
		{"literal", `\'; DROP ROLE x; --`, `E'\\''; DROP ROLE x; --'`},
	}
	for _, tc := range tests {
		got := PgQuoteLiteral(tc.in)
		if tc.fn == "ident" {
			got = PgQuoteIdent(tc.in)
		}
		if got != tc.want {
			t.Errorf("%s(%q) = %s, want %s", tc.fn, tc.in, got, tc.want)
		}
	}
}
