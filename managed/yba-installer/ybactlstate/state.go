package ybactlstate

import (
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/components/ybactl"
)

const (
	StateFileName     = ".yba_installer.state"
	schemaVersion int = 1
)

type State struct {
	Version         string        `json:"version"`
	RootInstall     string        `json:"root_install"`
	Username        string        `json:"username"`
	Postgres        PostgresState `json:"postgres"`
	Ybdb            YbdbState     `json:"ybdb"`
	_internalFields internalFields
}

type PostgresState struct {
	UseExisting bool `json:"UseExisting"`
	IsEnabled   bool `json:"enabled"`
}

func New() *State {
	return &State{
		Version:     ybactl.Version,
		RootInstall: viper.GetString("installRoot"),
		Username:    viper.GetString("service_username"),
		Postgres: PostgresState{
			UseExisting: viper.GetBool("postgres.useExisting.enabled"),
			IsEnabled:   common.IsPostgresEnabled(),
		},
		Ybdb: YbdbState{
			IsEnabled: viper.GetBool("ybdb.install.enabled"),
		},
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
