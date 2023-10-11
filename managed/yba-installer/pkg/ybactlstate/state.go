package ybactlstate

import (
	"fmt"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/ybactl"
)

const (
	StateFileName     = ".yba_installer.state"
	schemaVersion int = 3
)

type State struct {
	Version         string                   `json:"version"`
	RootInstall     string                   `json:"root_install"`
	Username        string                   `json:"username"`
	Postgres        PostgresState            `json:"postgres"`
	CurrentStatus   status                   `json:"current_status"`
	Replicated      ReplicatedMigrationState `json:"replicated_migration"`
	_internalFields internalFields
}

type PostgresState struct {
	UseExisting bool `json:"UseExisting"`
	LdapEnabled bool `json:"ldap_enabled"`
}

type ReplicatedMigrationState struct {
	PrometheusFileUser  uint32 `json:"prometheus_file_user"`
	PrometheusFileGroup uint32 `json:"prometheus_file_group"`
}

func New() *State {
	return &State{
		Version:     ybactl.Version,
		RootInstall: viper.GetString("installRoot"),
		Username:    viper.GetString("service_username"),
		Postgres: PostgresState{
			UseExisting: viper.GetBool("postgres.useExisting.enabled"),
			LdapEnabled: viper.GetBool("postgres.install.ldap_enabled"),
		},
		Replicated:    ReplicatedMigrationState{},
		CurrentStatus: UninstalledStatus,
		_internalFields: internalFields{
			ChangeID:      0,
			SchemaVersion: schemaVersion,
		},
	}
}

type internalFields struct {
	ChangeID      int `json:"change_id"`
	SchemaVersion int `json:"schema"`
}

// TransitionStatus will move the state from CurrentStatus to next, after first Validating the
// transition path. After updating CurrentStatus, the state will call StoreState to ensure it is
// updated on the filesystem.
func (s *State) TransitionStatus(next status) error {
	if !s.CurrentStatus.TransitionValid(next) {
		return fmt.Errorf("%w, cannot move from %s to %s",
			StatusTransitionError, s.CurrentStatus.String(), next.String())
	}
	s.CurrentStatus = next
	err := StoreState(s)
	if err != nil {
		return fmt.Errorf("could not transition to status %s, failed to save state: %w",
			next.String(), err)
	}
	return nil
}
