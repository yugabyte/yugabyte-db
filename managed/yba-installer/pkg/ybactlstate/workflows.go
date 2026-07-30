package ybactlstate

import (
	"fmt"
	"time"

	"github.com/spf13/viper"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// ValidateReconfig should be called before a reconfig, to make sure the new config doesnt' change
// "unchangable" values - like root install for example.
func (s State) ValidateReconfig() error {
	if viper.GetString("installRoot") != s.RootInstall {
		return fmt.Errorf("cannot change root install from %s", s.RootInstall)
	}
	if viper.GetBool("postgres.useExisting.enabled") != s.Postgres.UseExisting {
		return fmt.Errorf("cannot change postgres install type")
	}

	if viper.GetString("service_username") != s.Username {
		return fmt.Errorf("cannot change service username from %s", s.Username)
	}

	if viper.GetBool("postgres.install.ldap_enabled") != s.Postgres.LdapEnabled {
		return fmt.Errorf("cannot change postgres ldap configuration")
	}

	if viper.GetBool("as_root") != s.Config.AsRoot {
		return fmt.Errorf("cannot change as_root from %t", s.Config.AsRoot)
	}

	if err := ValidatePrometheusScrapeConfig(); err != nil {
		return err
	}

	return nil
}

// ValidatePrometheusScrapeConfig validates the prometheus scrape config.
// It checks that the scrape timeout is less than the scrape interval.
func ValidatePrometheusScrapeConfig() error {
	// Parse the scrape timeout and interval.
	scrapeTimeout, err := time.ParseDuration(viper.GetString("prometheus.scrapeTimeout"))
	if err != nil {
		return fmt.Errorf("cannot parse prometheus scrape timeout: %s", err.Error())
	}
	scrapeInterval, err := time.ParseDuration(viper.GetString("prometheus.scrapeInterval"))
	if err != nil {
		return fmt.Errorf("cannot parse prometheus scrape interval: %s", err.Error())
	}
	if scrapeTimeout > scrapeInterval {
		return fmt.Errorf("prometheus scrape timeout must be less than scrape interval")
	}

	return nil
}

type DbUpgradeWorkflow string

const (
	PgToYbdb   DbUpgradeWorkflow = "switchPgToYbdb"
	YbdbToPg   DbUpgradeWorkflow = "switchYbdbToPg"
	PgToPg     DbUpgradeWorkflow = "pgToPg"
	YbdbToYbdb DbUpgradeWorkflow = "YbdbToYbdb"
)

func (s State) GetDbUpgradeWorkFlow() DbUpgradeWorkflow {
	if viper.GetBool("postgres.useExisting.enabled") != s.Postgres.UseExisting {
		log.Fatal("cannot change existing postgres install type")
	}
	if viper.GetBool("ybdb.install.enabled") {
		if s.Ybdb.IsEnabled {
			return YbdbToYbdb
		}
		//Allow switching from postgres to ybdb.
		return PgToYbdb
	} else if s.Ybdb.IsEnabled {
		return YbdbToPg
	}
	return PgToPg
}
