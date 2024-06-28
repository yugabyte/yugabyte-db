package ybactlstate

import (
	"bytes"
	"fmt"
	"slices"
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
const promOomConfgMV = 5
const promTLSCipherSuites = 6

// Please do not use this in ybactlstate package, only use getSchemaVersion()
var schemaVersionCache = -1

func handleMigration(state *State) error {
	if err := updateSchemaTracking(state); err != nil {
		return err
	}
	nextSchema := 0
	endSchema := getSchemaVersion()
	for nextSchema < endSchema {
		nextSchema++
		if slices.Contains(state._internalFields.RunSchemas, nextSchema) {
			continue
		}
		migrate := getMigrationHandler(nextSchema)
		if migrate == nil {
			log.Debug("skipping migration " + strconv.Itoa(nextSchema) + " as it is not defined")
			continue
		}
		if err := migrate(state); err != nil {
			return err
		}
		state._internalFields.RunSchemas = append(state._internalFields.RunSchemas, nextSchema)
	}
	// StoreState in order to persist migration SchemaVersion
	return StoreState(state)
}

// Update how we track the schema version. Previously, we tracked the max version with the schema
// Version variable. Now, we want to explicitly track only the executed schemas. We will assume if
// this transfer needs to be run, all migrations below the tracked schema version have been run, and
// mark them as run.
func updateSchemaTracking(state *State) error {
	if state._internalFields.RunSchemas != nil {
		return nil
	}
	// allSchemaSlice returns the list for the current max schema, but we need specifically those
	// that have already been run - tracked previously by the state SchemaVersion
	state._internalFields.RunSchemas = allSchemaSlice()[:state._internalFields.SchemaVersion]
	log.DebugLF(fmt.Sprintf("updating schema tracking with run schemas of %v",
		state._internalFields.RunSchemas))
	return nil
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
	typeMap := map[string]string{
		"platform.port":                        "int",
		"platform.hsts_enabled":                "bool",
		"platform.useOauth":                    "bool",
		"platform.restartSeconds":              "int",
		"platform.proxy.enable":                "bool",
		"platform.proxy.java_http_proxy_port":  "int",
		"platform.proxy.java_https_proxy_port": "int",
		"postgres.install.enabled":             "bool",
		"postgres.install.port":                "int",
		"postgres.install.ldap_enabled":        "bool",
		"postgres.install.ldap_port":           "int",
		"postgres.install.secure_ldap":         "bool",
		"postgres.useExisting.enabled":         "bool",
		"postgres.useExisting.port":            "int",
		"prometheus.port":                      "int",
		"prometheus.restartSeconds":            "int",
		"prometheus.maxConcurrency":            "int",
		"prometheus.maxSamples":                "int",
		"prometheus.enableHttps":               "bool",
		"prometheus.enableAuth":                "bool",
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
			err = common.SetYamlValue(common.InputFile(), key, b)
			if err != nil {
				log.Warn("Error setting yaml value " + key + value)
				return err
			}
		}
	}
	common.InitViper()
	return nil
}

func migratePrometheusOOMConfig(state *State) error {

	if !viper.IsSet("prometheus.oomScoreAdjust") {
		viper.ReadConfig(bytes.NewBufferString(config.ReferenceYbaCtlConfig))
		err := common.SetYamlValue(common.InputFile(), "prometheus.oomScoreAdjust",
			viper.GetString("prometheus.oomScoreAdjust"))
		if err != nil {
			return fmt.Errorf("Error migrating prometheus OOM config: %s", err.Error())
		}
	}

	common.InitViper()
	return nil
}

func migratePrometheusTLSCipherSuites(state *State) error {
	log.Info("Entering migrating tls cipher suites")
	if !viper.IsSet("prometheus.allowedTLSCiphers") {
		viper.ReadConfig(bytes.NewBufferString(config.ReferenceYbaCtlConfig))
		err := common.SetYamlValue(common.InputFile(), "prometheus.allowedTLSCiphers",
			viper.GetStringSlice("prometheus.allowedTLSCiphers"))
		if err != nil {
			return fmt.Errorf("Error migrating prometheus tls cipher suite config: %s", err.Error())
		}
	}

	common.InitViper()
	return nil
}

// TODO: Also need to remember to update schemaVersion when adding migration! (automate testing?)
var migrations map[int]migrator = map[int]migrator{
	defaultMigratorValue: defaultMigrate,
	promConfigMV:         migratePrometheus,
	postgresUserMV:       migratePostgresUser,
	ymlTypeFixMV:         migrateYmlTypes,
	promOomConfgMV:       migratePrometheusOOMConfig,
	promTLSCipherSuites:  migratePrometheusTLSCipherSuites,
}

func getMigrationHandler(toSchema int) migrator {
	m, ok := migrations[toSchema]
	if !ok {
		return nil
	}
	return m
}

func getSchemaVersion() int {
	if schemaVersionCache == -1 {
		for k := range migrations {
			if k > schemaVersionCache {
				schemaVersionCache = k
			}
		}
	}
	return schemaVersionCache
}
