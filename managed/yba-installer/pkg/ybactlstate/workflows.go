package ybactlstate

import (
	"fmt"
	"time"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
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

	if viper.GetBool("fips.enabled") != s.Config.FipsEnabled {
		return fmt.Errorf("cannot change fips.enabled from %t: an existing YugabyteDB Anywhere "+
			"cannot be converted to or from FIPS mode. Set fips.enabled back to %t in %s",
			s.Config.FipsEnabled, s.Config.FipsEnabled, common.InputFile())
	}

	if err := ValidatePrometheusScrapeConfig(); err != nil {
		return err
	}

	return nil
}

// ValidateReinstall is called before installing over a soft-cleaned install. Its data is still in
// place, so FIPS mode cannot change any more than it can on a running install.
func (s State) ValidateReinstall() error {
	if s.CurrentStatus != SoftCleanStatus || viper.GetBool("fips.enabled") == s.Config.FipsEnabled {
		return nil
	}
	return fmt.Errorf("fips.enabled is %t, but the data kept by the previous yba-ctl clean "+
		"belongs to an install with fips.enabled %t. Install with fips.enabled: %t, or remove "+
		"that data first with yba-ctl clean --all",
		!s.Config.FipsEnabled, s.Config.FipsEnabled, s.Config.FipsEnabled)
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
