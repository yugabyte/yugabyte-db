package ybactlstate

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

// LoadState will read the state from the file store, and perform migrations to the latest schema
// as needed.
func LoadState() (*State, error) {
	//var state *State
	state := &State{}
	sp := filepath.Join(common.YbactlInstallDir(), StateFileName)
	sf, err := os.Open(sp)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return state, nil
		}
		return state, err
	}
	if err := json.NewDecoder(sf).Decode(state); err != nil {
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
	f, err := os.Create(sp)
	if err != nil {
		return nil
	}
	if err := json.NewEncoder(f).Encode(&state); err != nil {
		return err
	}
	return nil
}
