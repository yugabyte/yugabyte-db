/*
* Copyright (c) YugabyteDB, Inc.
 */

package checks

import (
	"fmt"
	"strconv"
	"strings"

	_ "github.com/lib/pq"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"golang.org/x/exp/slices"
)

var Postgres = &postgresCheck{
	"postgres",
	true,
	[]int{14}, // supportedMajorVersions
}

type postgresCheck struct {
	name                   string
	skipAllowed            bool
	supportedMajorVersions []int
}

// Name gets the name of the check
func (p postgresCheck) Name() string {
	return p.name
}

// SkipAllowed returns if the check can be skipped
func (p postgresCheck) SkipAllowed() bool {
	return p.skipAllowed
}

// ValidateUserPostgres validates if user postgres is initialized.
func (p postgresCheck) Execute() Result {
	res := Result{
		Check:  p.name,
		Status: StatusPassed,
	}

	useExisting := viper.GetBool("postgres.useExisting.enabled")
	install := viper.GetBool("postgres.install.enabled")
	if (useExisting && install) || (!useExisting && !install) {
		res.Status = StatusCritical
		res.Error = fmt.Errorf(
			"exactly one of postgres.useExisting.enabled and" +
				"postgres.install.enabled should be set to true")
		return res
	}

	if useExisting {
		pgDumpPath := viper.GetString("postgres.useExisting.pg_dump_path")
		pgRestorePath := viper.GetString("postgres.useExisting.pg_restore_path")
		if pgDumpPath == "" || pgRestorePath == "" {
			res.Status = StatusCritical
			res.Error = fmt.Errorf(
				"both pg_dump_path and pg_restore_path must be set when using existing Postgres server")
			return res
		}
	}

	if install {
		return res
	}

	err := p.testExistingPostgres()
	if err != nil {
		log.Error("failed subtest 'testExistingPostgres': " + err.Error())
		res.Status = StatusCritical
		res.Error = err
		return res
	}

	err = p.testPgcryptoAvailable()
	if err != nil {
		log.Error("failed subtest 'testPgcryptoAvailable': " + err.Error())
		res.Status = StatusCritical
		res.Error = err
		return res
	}

	if viper.GetBool("perfAdvisor.enabled") {
		if err := p.testPerfAdvisorDatabase(); err != nil {
			log.Error("failed subtest 'testPerfAdvisorDatabase': " + err.Error())
			res.Status = StatusCritical
			res.Error = err
			return res
		}
	}

	return res
}

// testPerfAdvisorDatabase checks that the existing postgres server can hold Perf Advisor's
// database: it is already there, or the configured user may create it.
func (p postgresCheck) testPerfAdvisorDatabase() error {
	exists, canCreate, err := common.ExistingPostgresDatabaseState(common.PerfAdvisorDBName)
	if err != nil {
		return err
	}
	if exists || canCreate {
		return nil
	}
	return fmt.Errorf("perfAdvisor.enabled needs a database named %s on the existing postgres "+
		"server, and it does not exist and user %s may not create databases. Create it, grant the "+
		"user CREATEDB, or set perfAdvisor.enabled to false", common.PerfAdvisorDBName,
		viper.GetString("postgres.useExisting.username"))
}

// If the user has specified their own postgres db endpoint, this method attempts to
// validate it
func (p postgresCheck) testExistingPostgres() error {
	db, nonPwdConnStr, err := common.GetPostgresConnection("yugaware")
	if err != nil {
		return fmt.Errorf("Could not connect to db with connStr %s : error %s", nonPwdConnStr, err)
	}

	log.Debug("Fetching server version")
	rows, err := db.Query("SHOW server_version;")
	if err != nil {
		return fmt.Errorf("Could not connect to db with connStr %s : error %s", nonPwdConnStr, err)
	}
	defer rows.Close()

	if !rows.Next() {
		return fmt.Errorf("Could not query version from db %s", err)
	}

	var pgVersion string
	err = rows.Scan(&pgVersion)
	if err != nil {
		return fmt.Errorf("Could not read version from postgres %s", err)
	}
	log.Debug(fmt.Sprintf("Postgres server version is %s", pgVersion))

	pgMajorVersion := -1
	pgMajorVersion, _ = strconv.Atoi(strings.Split(pgVersion, ".")[0])
	if !slices.Contains(p.supportedMajorVersions, pgMajorVersion) {
		return fmt.Errorf("Unsupported postgres major version %d", pgMajorVersion)
	}
	log.Debug("validated existing postgres version")
	return nil
}

// testPgcryptoAvailable checks that YBA's V195 migration will be able to create pgcrypto. It must
// not create the extension itself: pgcrypto is a trusted extension on PG 13+, so when a
// non-superuser creates it, public.gen_random_uuid() is owned by the bootstrap superuser, and
// V93's CREATE OR REPLACE FUNCTION gen_random_uuid() then fails with "must be owner of function
// gen_random_uuid".
func (p postgresCheck) testPgcryptoAvailable() error {
	db, nonPwdConnStr, err := common.GetPostgresConnection("yugaware")
	if err != nil {
		return fmt.Errorf("Could not connect to db with connStr %s : error %s", nonPwdConnStr, err)
	}
	defer db.Close()
	log.Debug("checking pgcrypto is an available extension")
	var available, installed, canCreate bool
	err = db.QueryRow(`SELECT
		EXISTS (SELECT 1 FROM pg_available_extensions WHERE name = 'pgcrypto'),
		EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pgcrypto'),
		has_database_privilege(current_database(), 'CREATE')`).
		Scan(&available, &installed, &canCreate)
	if err != nil {
		return fmt.Errorf("could not check for the pgcrypto extension: %w", err)
	}
	return pgcryptoError(available, installed, canCreate,
		viper.GetString("postgres.useExisting.username"))
}

// pgcryptoError says why YBA could not create pgcrypto in the yugaware database, or returns nil if
// it can or the extension is already there.
func pgcryptoError(available, installed, canCreate bool, user string) error {
	if installed {
		return nil
	}
	if !available {
		return fmt.Errorf("the pgcrypto extension is not available on the postgres server. " +
			"Install the server's contrib package")
	}
	if !canCreate {
		return fmt.Errorf("user %s needs the CREATE privilege on database yugaware to create the "+
			"pgcrypto extension. Grant it, or make the user the owner of the database", user)
	}
	return nil
}
