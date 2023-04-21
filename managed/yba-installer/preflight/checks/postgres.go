/*
* Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"strconv"
	"strings"

	_ "github.com/lib/pq"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"golang.org/x/exp/slices"
)

var Postgres = &postgresCheck{
	"postgres",
	true,
	[]int{10, 14}, // supportedMajorVersions
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

	if (useExisting) {
		pgDumpPath := viper.GetString("postgres.useExisting.pg_dump_path")
		pgRestorePath := viper.GetString("postgres.useExisting.pg_restore_path")
		if (pgDumpPath == "" || pgRestorePath == "") {
			res.Status = StatusCritical
			res.Error = fmt.Errorf(
				"both pg_dump_path and pg_restore_path must be set when using existing Postgres server")
			return res
		}
	}

	if install {
		return res
	}

	res = p.testExistingPostgres()
	if res.Status != StatusPassed {
		return res
	}

	out := shell.Run("pg_dump", "--help")
	if !out.SucceededOrLog() {
		res.Error = fmt.Errorf("pg_dump has to be installed on the host (error %w)", out.Error)
		res.Status = StatusCritical
		return res
	}

	return res
}

// If the user has specified their own postgres db endpoint, this method attempts to
// validate it
func (p postgresCheck) testExistingPostgres() Result {

	res := Result{
		Check:  p.name,
		Status: StatusPassed,
	}

	db, nonPwdConnStr, err := common.GetPostgresConnection("yugaware")
	if err != nil {
		res.Error = fmt.Errorf("Could not connect to db with connStr %s : error %s", nonPwdConnStr, err)
		res.Status = StatusCritical
		return res
	}
	log.Debug("Fetching server version")
	rows, err := db.Query("SHOW server_version;")
	if err != nil {
		res.Error = fmt.Errorf("Could not connect to db with connStr %s : error %s", nonPwdConnStr, err)
		res.Status = StatusCritical
		log.Info(res.Error.Error())
		return res
	}
	defer rows.Close()

	if !rows.Next() {
		res.Error = fmt.Errorf("Could not query version from db %s", err)
		res.Status = StatusCritical
		log.Info(res.Error.Error())
		return res
	}

	var pgVersion string
	err = rows.Scan(&pgVersion)
	if err != nil {
		res.Error = fmt.Errorf("Could not read version from postgres %s", err)
		res.Status = StatusCritical
		log.Info(res.Error.Error())
		return res
	}
	log.Debug(fmt.Sprintf("Postgres server version is %s", pgVersion))

	pgMajorVersion := -1
	pgMajorVersion, _ = strconv.Atoi(strings.Split(pgVersion, ".")[0])
	if !slices.Contains(p.supportedMajorVersions, pgMajorVersion) {
		res.Error = fmt.Errorf("Unsupported postgres major version %d", pgMajorVersion)
		res.Status = StatusCritical
		log.Info(res.Error.Error())
		return res
	}

	return res
}
