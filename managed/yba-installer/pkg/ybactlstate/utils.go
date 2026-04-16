package ybactlstate

import (
	"errors"
	"os"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

func Initialize() (*State, error) {
	state, err := LoadState()
	if err != nil {
		if !errors.Is(err, os.ErrNotExist) {
			return nil, err
		}
		log.Debug("fresh install, creating initial state")
		state = New()
	} else {
		log.Debug("found a previous state, continue install with context")
	}
	return state, nil
}
