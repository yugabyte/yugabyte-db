package ybactlstate

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

func TestMigratePgInitialMajorVersion(t *testing.T) {
	oldVersion := common.PostgresVersion
	t.Cleanup(func() { common.PostgresVersion = oldVersion; viper.Reset() })
	common.PostgresVersion = "18.6"
	viper.Set("installRoot", t.TempDir())
	confDir := filepath.Join(common.GetActiveSymlink(), "pgsql/conf")
	if err := os.MkdirAll(confDir, 0755); err != nil {
		t.Fatal(err)
	}
	dataDir := filepath.Join(common.GetDataRoot(), "postgres")

	// No cluster yet: the bundled version.
	state := &State{}
	if err := migratePgInitialMajorVersion(state); err != nil {
		t.Fatal(err)
	}
	if state.Postgres.InitialMajorVersion != 18 {
		t.Fatalf("no cluster: InitialMajorVersion = %d, want 18", state.Postgres.InitialMajorVersion)
	}

	// A cluster the conf points to but that cannot be read must not be guessed at.
	conf := "data_directory = '" + dataDir + "'\n"
	if err := os.WriteFile(filepath.Join(confDir, "postgresql.conf"), []byte(conf), 0644); err != nil {
		t.Fatal(err)
	}
	if err := migratePgInitialMajorVersion(&State{}); err == nil {
		t.Fatal("unreadable cluster version did not fail the migration")
	}

	// The running cluster wins over the bundled version.
	if err := os.MkdirAll(dataDir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dataDir, "PG_VERSION"), []byte("14\n"), 0644); err != nil {
		t.Fatal(err)
	}
	state = &State{}
	if err := migratePgInitialMajorVersion(state); err != nil {
		t.Fatal(err)
	}
	if state.Postgres.InitialMajorVersion != 14 {
		t.Fatalf("running 14: InitialMajorVersion = %d, want 14", state.Postgres.InitialMajorVersion)
	}
}
