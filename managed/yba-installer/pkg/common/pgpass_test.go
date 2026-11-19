package common

import (
	"testing"

	"github.com/spf13/viper"
)

func TestPgpassEntryMatchesAllDatabases(t *testing.T) {
	viper.Set("postgres.useExisting.host", "db.example.com")
	viper.Set("postgres.useExisting.port", "5432")
	viper.Set("postgres.useExisting.username", "yba")
	viper.Set("postgres.useExisting.password", "secret")
	t.Cleanup(viper.Reset)

	want := "db.example.com:5432:*:yba:secret"
	if got := PgpassEntry(); got != want {
		t.Errorf("PgpassEntry() = %q, want %q", got, want)
	}
}
