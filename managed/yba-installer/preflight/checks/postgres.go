/*
* Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var Postgres = &postgresCheck{}

type postgresCheck struct {
	name         string
	warningLevel string
}

func (p postgresCheck) Name() string {
	return p.name
}

func (p postgresCheck) WarningLevel() string {
	return p.warningLevel
}

// ValidateUserPostgres validates if user postgres is initialized.
func (p postgresCheck) Execute() error {
	if !viper.GetBool("postgres.bringOwn") {
		log.Debug("skipping " + p.name + " as user we will provide postgres")
		return nil
	}
	port := config.GetYamlPathData("postgres.port")
	username := config.GetYamlPathData(".postgres.username")
	password := config.GetYamlPathData(".postgres.password")

	// Logging parsed user provided port, username, and password for
	// debugging purposes.
	log.Debug("User provided Postgres port: " + port)

	checkPostgresCommand := "PGPASSWORD= " + password +
		" psql -p " + port + " -U " + username + " -d yugaware -c '\\d'"
	command := "bash"
	args := []string{"-c", checkPostgresCommand}

	if _, err := common.ExecuteBashCommand(command, args); err != nil {
		log.Info("User provided Postgres not initialized properly! See the" +
			" above error message for more details.")
		return err
	}

	return nil
}
