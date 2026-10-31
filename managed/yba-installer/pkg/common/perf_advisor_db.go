// Copyright (c) YugabyteDB, Inc.

package common

import (
	"fmt"
	"strings"

	"github.com/spf13/viper"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// PerfAdvisorDBName is the database Perf Advisor keeps its data in, next to yugaware.
const PerfAdvisorDBName = "ts"

// PerfAdvisorDBConfig is the datasource Perf Advisor is rendered with.
type PerfAdvisorDBConfig struct {
	Host     string
	Port     string
	Username string
	Password string
	Name     string
}

// PerfAdvisorDatabase is the postgres Perf Advisor uses: the server yba-ctl installs, or the one
// postgres.useExisting points at, where YBA's own yugaware database also lives.
func PerfAdvisorDatabase() PerfAdvisorDBConfig {
	if viper.GetBool("postgres.useExisting.enabled") {
		return PerfAdvisorDBConfig{
			Host:     viper.GetString("postgres.useExisting.host"),
			Port:     viper.GetString("postgres.useExisting.port"),
			Username: viper.GetString("postgres.useExisting.username"),
			Password: viper.GetString("postgres.useExisting.password"),
			Name:     PerfAdvisorDBName,
		}
	}
	return PerfAdvisorDBConfig{
		Host:     "localhost",
		Port:     viper.GetString("postgres.install.port"),
		Username: "postgres",
		Password: viper.GetString("postgres.install.password"),
		Name:     PerfAdvisorDBName,
	}
}

// ExistingPostgresDatabaseState reports whether dbName exists on the postgres.useExisting server
// and, if not, whether the configured user may create it. It connects through the yugaware
// database, the one that server is required to have.
func ExistingPostgresDatabaseState(dbName string) (exists bool, canCreate bool, err error) {
	db, connStr, err := GetPostgresConnection("yugaware")
	if err != nil {
		return false, false, fmt.Errorf("could not connect to %s: %w", connStr, err)
	}
	defer db.Close()
	if err := db.QueryRow("SELECT EXISTS (SELECT 1 FROM pg_database WHERE datname = $1)",
		dbName).Scan(&exists); err != nil {
		return false, false, fmt.Errorf("could not look up database %s: %w", dbName, err)
	}
	if exists {
		return true, false, nil
	}
	if err := db.QueryRow(
		"SELECT rolcreatedb OR rolsuper FROM pg_roles WHERE rolname = current_user").
		Scan(&canCreate); err != nil {
		return false, false, fmt.Errorf("could not read the privileges of the postgres user: %w", err)
	}
	return false, canCreate, nil
}

// EnsureExistingPostgresDatabase creates dbName on the postgres.useExisting server unless it is
// already there. The owner is the configured user, which is also the one that connects to it.
func EnsureExistingPostgresDatabase(dbName string) error {
	exists, canCreate, err := ExistingPostgresDatabaseState(dbName)
	if err != nil {
		return err
	}
	if exists {
		return nil
	}
	if !canCreate {
		return fmt.Errorf("database %s does not exist on the postgres server at %s and user %s "+
			"may not create databases: create it, or grant the user CREATEDB", dbName,
			viper.GetString("postgres.useExisting.host"), viper.GetString("postgres.useExisting.username"))
	}
	db, connStr, err := GetPostgresConnection("yugaware")
	if err != nil {
		return fmt.Errorf("could not connect to %s: %w", connStr, err)
	}
	defer db.Close()
	log.Info("Creating database " + dbName + " on the existing postgres server")
	if _, err := db.Exec(fmt.Sprintf("CREATE DATABASE %s", PgQuoteIdentifier(dbName))); err != nil {
		return fmt.Errorf("could not create database %s: %w", dbName, err)
	}
	return nil
}

// PgQuoteIdentifier quotes a postgres identifier, like quote_ident().
func PgQuoteIdentifier(name string) string {
	return `"` + strings.ReplaceAll(name, `"`, `""`) + `"`
}
