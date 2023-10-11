package ybactlstate

import (
	"fmt"

	"github.com/spf13/viper"
)

// ValidateReconfig should be called before a reconfig, to make sure the new config doesnt' change
// "unchangable" values - like root install for example.
func (s State) ValidateReconfig() error {
	if viper.GetString("installRoot") != s.RootInstall {
		return fmt.Errorf("cannot change root install from %s", s.RootInstall)
	}
	if viper.GetBool("postgres.useExisting") != s.Postgres.UseExisting {
		return fmt.Errorf("cannot change postgres install type")
	}

	if viper.GetString("service_username") != s.Username {
		return fmt.Errorf("cannot change service username from %s", s.Username)
	}

	if viper.GetBool("postgres.install.ldap_enabled") != s.Postgres.LdapEnabled {
		return fmt.Errorf("cannot change postgres ldap configuration")
	}
	return nil
}
