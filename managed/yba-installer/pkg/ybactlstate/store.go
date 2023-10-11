package ybactlstate

import (
	"encoding/json"
	"fmt"
	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// LoadState will read the state from the file store, and perform migrations to the latest schema
// as needed.
func LoadState() (*State, error) {
	state := &State{}
	sp := filepath.Join(common.YbactlInstallDir(), StateFileName)
	sf, err := fs.Open(sp)
	if err != nil {
		// Debug log only, as the file not existing will may not be an error, but expected
		logging.Debug(fmt.Sprintf("failed to open state file %s - %s", sp, err.Error()))
		return state, err
	}
	if err := json.NewDecoder(sf).Decode(state); err != nil {
		logging.Error(fmt.Sprintf("could not unmarshal the state json: %s", err.Error()))
		return state, err
	}
	err = handleMigration(state)
	return state, err
}

// StoreState will write the state
func StoreState(state *State) error {
	// TODO: THis will update the change id even if no changes are made. At this point, we don't
	// have a way to track if state changes have been made or not.
	state._internalFields.ChangeID++
	sp := filepath.Join(common.YbactlInstallDir(), StateFileName)
	f, err := fs.Create(sp)
	if err != nil {
		return err
	}
	if err := json.NewEncoder(f).Encode(&state); err != nil {
		return err
	}
	return nil
}
