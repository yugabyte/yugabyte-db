package ybactlstate

import (
	"fmt"

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
	// Check none of the installed services are changing:
	if s.Services.PerfAdvisor != viper.GetBool("perfAdvisor.enabled") {
		return fmt.Errorf("cannot change perf advisor service from %t", s.Services.PerfAdvisor)
	}
	// Platform is enabled if perf advisor is false or if it is enabled withPlatform.
	if s.Services.Platform != (!viper.GetBool("perfAdvisor.enabled") || viper.GetBool("perfAdvisor.withPlatform")) {
		return fmt.Errorf("cannot change platform service from %t", s.Services.Platform)
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
