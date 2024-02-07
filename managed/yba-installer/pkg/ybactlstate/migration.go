package ybactlstate

import (
	"bytes"
	"fmt"
	"strconv"

	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

const defaultMigratorValue = -1
const promConfigMV = 2
const postgresUserMV = 3
const ymlTypeFixMV = 4

func handleMigration(state *State) error {
	for state._internalFields.SchemaVersion < schemaVersion {
		nextSchema := state._internalFields.SchemaVersion + 1
		migrate := getMigrationHandler(nextSchema)
		if err := migrate(state); err != nil {
			return err
		}
		state._internalFields.SchemaVersion = nextSchema
	}
	// StoreState in order to persist migration SchemaVersion
	return StoreState(state)
}

type migrator func(state *State) error

// Migrate on default is a no-op, mainly assuming that the default values given to struct fields
// are sufficient.
func defaultMigrate(state *State) error {
	return nil
}

func migratePrometheus(state *State) error {

	promSettings := [3]string{
		"prometheus.timeout", "prometheus.scrapeInterval", "prometheus.scrapeTimeout"}
	for _, set := range promSettings {
		val := viper.GetInt(set)
		if val != 0 {
			err := common.SetYamlValue(common.InputFile(), set, fmt.Sprintf("%ds", val))
			if err != nil {
				return err
			}
		}
	}

	if !viper.IsSet("prometheus.retentionTime") {
		viper.ReadConfig(bytes.NewBufferString(config.ReferenceYbaCtlConfig))
		err := common.SetYamlValue(common.InputFile(), "prometheus.retentionTime",
			viper.GetString("prometheus.retentionTime"))
		if err != nil {
			return fmt.Errorf("Error migrating prometheus retention time: %s", err.Error())
		}
	}

	common.InitViper()
	return nil
}

func migratePostgresUser(state *State) error {
	if !viper.IsSet("postgres.install.username") {
		viper.ReadConfig(bytes.NewBufferString(config.ReferenceYbaCtlConfig))
		if err := common.SetYamlValue(common.InputFile(), "postgres.install.username",
			viper.GetString("postgres.install.username")); err != nil {
				return fmt.Errorf("Error migrating postgres user: %s", err.Error())
			}
	}
	common.InitViper()
	return nil
}

func migrateYmlTypes(state *State) error {
	log.Info("Entering migrateYmlTypes")
	typeMap := map[string]string {
		"platform.port": 													"int",
		"platform.hsts_enabled": 									"bool",
		"platform.useOauth": 											"bool",
		"platform.restartSeconds": 								"int",
		"platform.proxy.enable": 									"bool",
		"platform.proxy.java_http_proxy_port": 		"int",
		"platform.proxy.java_https_proxy_port": 	"int",
		"postgres.install.enabled": 							"bool",
		"postgres.install.port": 									"int",
		"postgres.install.ldap_enabled": 					"bool",
		"postgres.install.ldap_port": 						"int",
		"postgres.install.secure_ldap": 					"bool",
		"postgres.useExisting.enabled": 					"bool",
		"postgres.useExisting.port": 							"int",
		"prometheus.port": 												"int",
		"prometheus.restartSeconds": 							"int",
		"prometheus.maxConcurrency": 							"int",
		"prometheus.maxSamples": 									"int",
		"prometheus.enableHttps": 								"bool",
		"prometheus.enableAuth": 									"bool",
	}

	for key, typeStr := range typeMap {
		value := viper.GetString(key)
		if len(value) == 0 || value == "" {
			log.Warn("Could not find value for key " + key)
			continue
		}
		switch typeStr {
		case "int":
			i, err := strconv.Atoi(value)
			if err != nil {
				log.Warn(fmt.Sprintf("Could not convert %s: %s int.", key, value))
				return err
			}
			err = common.SetYamlValue(common.InputFile(), key, i)
			if err != nil {
				log.Warn("error setting yaml value " + key + value)
				return err
			}
		case "bool":
			b, err := strconv.ParseBool(value)
			if err != nil {
				log.Warn(fmt.Sprintf("Could not convert %s: %s to bool.", key, value))
				return err
			}
			err  = common.SetYamlValue(common.InputFile(), key, b)
			if err != nil {
				log.Warn("Error setting yaml value " + key + value)
				return err
			}
		}
	}
	common.InitViper()
	return nil
}

// TODO: Also need to remember to update schemaVersion when adding migration! (automate testing?)
var migrations map[int]migrator = map[int]migrator{
	defaultMigratorValue: defaultMigrate,
	promConfigMV: migratePrometheus,
	postgresUserMV: migratePostgresUser,
	ymlTypeFixMV: migrateYmlTypes,
}

func getMigrationHandler(toSchema int) migrator {
	m, ok := migrations[toSchema]
	if !ok {
		m = migrations[defaultMigratorValue]
	}
	return m
}
