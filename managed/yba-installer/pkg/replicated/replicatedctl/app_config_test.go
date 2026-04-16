package replicatedctl

import (
	"encoding/json"
	"testing"
)

// TestAppConfigUnmarshal unmarshal json from replicatedctl app-config export
func TestAppConfigUnmarshal(t *testing.T) {
	cfgStr := `{
		"normal": {
			"value": "val1"
		},
		"boolFalse": {
			"value": "0"
		},
		"boolTrue": {
			"value": "1"
		},
		"empty": {}
	}`

	var appCfg AppConfig = AppConfig{make(map[string]ConfigEntry)}
	err := json.Unmarshal([]byte(cfgStr), &appCfg)
	if err != nil {
		t.Errorf("failed to unmarshall app config: %s", err)
	}
	c := appCfg.Get("normal")
	if c.Name != "normal" || c.Value != "val1" {
		t.Errorf("'normal' config entry does not match")
	}

	c = appCfg.Get("boolFalse")
	ok, err := c.Bool()
	if ok || err != nil {
		t.Errorf("'boolFalse' config entry does not match")
	}

	c = appCfg.Get("boolTrue")
	ok, err = c.Bool()
	if !ok || err != nil {
		t.Errorf("'boolTrue' config entry does not match")
	}

	c = appCfg.Get("empty")
	if c.Value != "" {
		t.Errorf("'empty' config entry does not match")
	}
}

// TestConfigEntryBool validates converting config entry value from string to bool
func TestConfigEntryBool(t *testing.T) {
	ce := ConfigEntry{Name: "true", Value: "1"}
	ok, err := ce.Bool()
	if err != nil {
		t.Errorf("errored parsing bool: %s", err)
	}
	if !ok {
		t.Errorf("expected true, got false")
	}

	ce = ConfigEntry{Name: "false", Value: "0"}
	ok, err = ce.Bool()
	if err != nil {
		t.Errorf("errored parsing bool: %s", err)
	}
	if ok {
		t.Errorf("expected false, got true")
	}
	ce = ConfigEntry{Name: "false", Value: "0123"}
	ok, err = ce.Bool()
	if err == nil {
		t.Errorf("Expected an error, but instead parsed a bool")
	}
}

func TestAppConfigViewUnmarshal(t *testing.T) {
	cfgStr := `[
		{
			"Name": "app",
			"Items": [
				 {
					"Name": "item1",
					"Value": "item 1 value"
				}
			]
		},
		{
			"Name": "db",
			"Items": [
				 {
					"Name": "boolTrue",
					"Value": "1"
				},
				{
					"Name": "boolFalse",
					"Value": "0"
				}
			]
		}
	]`

	var appView AppView = AppView{
		viewItems: make(map[string]ViewItem),
	}
	err := json.Unmarshal([]byte(cfgStr), &appView)
	if err != nil {
		t.Errorf("failed to unmarshall app view: %s", err)
	}
	c, err := appView.Get("item1")
	if err != nil {
		t.Errorf("unexpected error: %s", err)
	}
	if c.Name != "item1" || c.Value != "item 1 value" {
		t.Errorf("'item1' config entry does not match")
	}

	c, err = appView.Get("boolFalse")
	if err != nil {
		t.Errorf("unexpected error: %s", err)
	}
	if c.Name != "boolFalse" || c.Value != "0" {
		t.Errorf("'boolFalse' config entry does not match")
	}
}
