package ybactlstate

import (
	"encoding/json"
	"fmt"
)

type _stateAlias State

type _stateEncoder struct {
	_stateAlias
	Internal internalFields `json:"__internal"`
}

// UnmarshalJSON into state struct. Allows un-exported internal fields"
func (s *State) UnmarshalJSON(data []byte) error {
	var state _stateAlias
	if err := json.Unmarshal(data, &state); err != nil {
		return err
	}
	*s = State(state)
	var t map[string]interface{}
	if err := json.Unmarshal(data, &t); err != nil {
		return err
	}
	internal, ok := t["__internal"]
	if !ok {
		return fmt.Errorf("invalid state, no __internal field")
	}
	iData, err := json.Marshal(internal)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(iData, &s._internalFields); err != nil {
		return err
	}
	return nil
}

// MarshalJSON out of state struct, allows un-exported internal fields.
func (s State) MarshalJSON() ([]byte, error) {
	state := _stateEncoder{_stateAlias(s), s._internalFields}
	return json.Marshal(state)
}
