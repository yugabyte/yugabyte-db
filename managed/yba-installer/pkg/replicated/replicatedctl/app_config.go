package replicatedctl

import (
	"encoding/json"
	"fmt"
	"strconv"

	"errors"
)

// NilConfigEntry for when no config entry is found for a name
var NilConfigEntry ConfigEntry

// ErrorInvalidAppConfig when the full app-config cannot be processed
var ErrorInvalidAppConfig error = errors.New("invalid app config")

// ErrorConfigType for a single config entry not having the expected type
var ErrorConfigType error = errors.New("invalid type for app config value")

// ConfigEntry is a single config name and value
type ConfigEntry struct {
	Name  string
	Value string
}

// Bool converts the value from a string
func (ce ConfigEntry) Bool() (bool, error) {
	switch ce.Value {
	case "0":
		return false, nil
	case "1":
		return true, nil
	default:
		return false, fmt.Errorf(
			"%w: %s cannot be converted from string to bool", ErrorConfigType, ce.Name)
	}
}

// Int converts the value from a string
func (ce ConfigEntry) Int() (int, error) {
	i, err := strconv.Atoi(ce.Value)
	if err != nil {
		return i, fmt.Errorf("%w: %s cannot be converted to an int", ErrorConfigType, ce.Name)
	}
	return i, nil
}

// AppConfig is the full user config for an app
type AppConfig struct {
	ConfigEntries map[string]ConfigEntry
}

// Get returns a specific config entry, or NilConfigEntry if it doesn't exist
func (ac AppConfig) Get(key string) ConfigEntry {
	entry, ok := ac.ConfigEntries[key]
	if !ok {
		return NilConfigEntry
	}
	return entry
}

// EntriesAsSlice returns a slice of ConfigEntries, instead of keeping as a map
func (ac AppConfig) EntriesAsSlice() []ConfigEntry {
	var entries []ConfigEntry = make([]ConfigEntry, 0)
	for _, ce := range ac.ConfigEntries {
		entries = append(entries, ce)
	}
	return entries
}

// UnmarshalJSON returned from replicatedctl app-config export into our AppConfig
func (ac *AppConfig) UnmarshalJSON(data []byte) error {
	var configs map[string]any
	err := json.Unmarshal(data, &configs)
	if err != nil {
		return err
	}

	for k, v := range configs {
		if _, ok := ac.ConfigEntries[k]; ok {
			return fmt.Errorf("%w: found config %s multiple times", ErrorInvalidAppConfig, k)
		}
		config, ok := v.(map[string]any)
		if !ok {
			return fmt.Errorf("%w: config %s is not in the correct format", ErrorInvalidAppConfig, k)
		}
		rawVal, ok := config["value"]
		var value string
		// Value is not given - the config is not populated
		if !ok {
			value = ""
		} else {
			value, ok = rawVal.(string)
			if !ok {
				return fmt.Errorf("%w: config %s value is not a string type", ErrorInvalidAppConfig, k)
			}
		}
		CE := ConfigEntry{
			Name:  k,
			Value: value,
		}
		ac.ConfigEntries[k] = CE
	}
	return nil
}

// AppConfigExport get the config of the replicated app
// CMD: replicatedctl app-config export
func (r *ReplicatedCtl) AppConfigExport() (AppConfig, error) {
	var ac AppConfig = AppConfig{ConfigEntries: make(map[string]ConfigEntry)}
	raw, err := r.run("app-config", "export")
	if err != nil {
		return ac, fmt.Errorf("failed to export app config from replicated: %w", err)
	}
	err = json.Unmarshal(raw, &ac)
	if err != nil {
		return ac, fmt.Errorf("failed to parse exported app config: %w", err)
	}
	return ac, err
}
