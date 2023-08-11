package replicatedctl

import (
	"encoding/json"
	"errors"
	"fmt"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
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

var replicatedToYbaCtl = map[string]string{
	"storage_path": "installRoot",
	"nginx_custom_port": "platform.port",
	"enable_proxy": "platform.proxy.enable",
	"http_proxy": "platform.proxy.http_proxy",
	"https_proxy": "platform.proxy.https_proxy",
	"no_proxy": "platform.proxy.no_proxy",
	"java_https_proxy_port": "platform.proxy.java_https_proxy_port",
	"java_https_proxy_host": "platform.proxy.java_https_proxy_host",
	"java_http_proxy_port": "platform.proxy.java_http_proxy_port",
	"java_http_proxy_host": "platform.proxy.java_http_proxy_host",
	"db_external_port": "postgres.install.port",
	// TODO: Need to allow dbuser customization and also pass it on towards taking dump/LDAP
	// "prometheus_retention": "prometheus.retentionTime",
	"prometheus_query_timeout": "prometheus.timeout",
	"prometheus_scrape_interval": "prometheus.scrapeInterval",
	"prometheus_scrape_timeout": "prometheus.scrapeTimeout",
	"prometheus_query_max_samples": "prometheus.maxSamples",
	"prometheus_query_concurrency": "prometheus.maxConcurrency",
	"prometheus_external_port": "prometheus.port",
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

// ExportYbaCtl writes existing replicated settings to /opt/yba-ctl/yba-ctl.yml
func (ac *AppConfig) ExportYbaCtl() error {
	config.WriteDefaultConfig()
	for _, e := range ac.EntriesAsSlice() {
		if ybaCtlPath, ok := replicatedToYbaCtl[e.Name]; ok {
			if b, err := e.Bool(); err == nil {
				common.SetYamlValue(common.InputFile(), ybaCtlPath, strconv.FormatBool(b))
			} else {
				i, err := e.Int()
				if strings.HasSuffix(ybaCtlPath, "port") && err == nil {
					common.SetYamlValue(common.InputFile(), ybaCtlPath, strconv.Itoa(i + 1))
				} else {
					// TODO: Hack to get around 30s -> 30 conversion in prometheus timing values
					common.SetYamlValue(common.InputFile(), ybaCtlPath, strings.TrimSuffix(e.Value, "s"))
				}
			}
		}
	}
	return nil
}
