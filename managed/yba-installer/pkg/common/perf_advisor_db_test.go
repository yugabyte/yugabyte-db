// Copyright (c) YugabyteDB, Inc.

package common

import (
	"testing"

	"github.com/spf13/viper"
)

func TestPerfAdvisorDatabaseFollowsThePostgresInUse(t *testing.T) {
	viper.Reset()
	defer viper.Reset()
	viper.Set("postgres.install.enabled", true)
	viper.Set("postgres.install.port", "5432")
	viper.Set("postgres.install.password", "local-secret")
	viper.Set("postgres.useExisting.enabled", false)
	viper.Set("postgres.useExisting.host", "pg.example.com")
	viper.Set("postgres.useExisting.port", "6432")
	viper.Set("postgres.useExisting.username", "yba")
	viper.Set("postgres.useExisting.password", "external-secret")

	got := PerfAdvisorDatabase()
	want := PerfAdvisorDBConfig{"localhost", "5432", "postgres", "local-secret", "ts"}
	if got != want {
		t.Errorf("installed postgres: got %+v, want %+v", got, want)
	}

	viper.Set("postgres.install.enabled", false)
	viper.Set("postgres.useExisting.enabled", true)
	got = PerfAdvisorDatabase()
	want = PerfAdvisorDBConfig{"pg.example.com", "6432", "yba", "external-secret", "ts"}
	if got != want {
		t.Errorf("existing postgres: got %+v, want %+v", got, want)
	}
}

func TestPgQuoteIdentifier(t *testing.T) {
	for in, want := range map[string]string{"ts": `"ts"`, `we"ird`: `"we""ird"`} {
		if got := PgQuoteIdentifier(in); got != want {
			t.Errorf("PgQuoteIdentifier(%q) = %s, want %s", in, got, want)
		}
	}
}
