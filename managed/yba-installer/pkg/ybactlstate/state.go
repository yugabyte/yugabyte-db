package ybactlstate

import (
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/ybactl"
)

const (
	StateFileName     = ".yba_installer.state"
	schemaVersion int = 3
)

type State struct {
	Version         string        `json:"version"`
	RootInstall     string        `json:"root_install"`
	Username        string        `json:"username"`
	Postgres        PostgresState `json:"postgres"`
	Ybdb            YbdbState     `json:"ybdb"`
	CurrentStatus   status        `json:"current_status"`
	_internalFields internalFields
}

type PostgresState struct {
	UseExisting bool `json:"UseExisting"`
	IsEnabled   bool `json:"enabled"`
	LdapEnabled	bool `json:"ldap_enabled"`
}

func New() *State {
	return &State{
		Version:     ybactl.Version,
		RootInstall: viper.GetString("installRoot"),
		Username:    viper.GetString("service_username"),
		Postgres: PostgresState{
			UseExisting: viper.GetBool("postgres.useExisting.enabled"),
			IsEnabled:   common.IsPostgresEnabled(),
			LdapEnabled: viper.GetBool("postgres.install.ldap_enabled"),
		},
		Ybdb: YbdbState{
			IsEnabled: viper.GetBool("ybdb.install.enabled"),
		},
		CurrentStatus: UninstalledStatus,
		_internalFields: internalFields{
			ChangeID:      0,
			SchemaVersion: schemaVersion,
		},
	}
}

type YbdbState struct {
	IsEnabled bool `json:"enabled"`
}

type internalFields struct {
	ChangeID      int `json:"change_id"`
	SchemaVersion int `json:"schema"`
}
