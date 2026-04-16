package replicatedctl

import (
	"encoding/json"
	"fmt"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

type AppView struct {
	ViewEntries []ViewEntry
	viewItems   map[string]ViewItem
}

// UnmarshalJSON returned from replicatedctl app-config export into our AppConfig
func (av *AppView) UnmarshalJSON(data []byte) error {
	var entries []ViewEntry
	err := json.Unmarshal(data, &entries)
	if err != nil {
		return err
	}
	av.ViewEntries = entries
	for _, entry := range entries {
		for _, item := range entry.Items {
			av.viewItems[item.Name] = item
		}
	}
	return nil
}

func (av AppView) Get(key string) (ViewItem, error) {
	item, ok := av.viewItems[key]
	if !ok {
		return item, fmt.Errorf("unknown app config key: %s", key)
	}
	return item, nil
}

func (av AppView) PrintKeyVals() {
	for k, v := range av.viewItems {
		fmt.Printf("%s: %s\n", k, v)
	}
}

type ViewEntry struct {
	Name        string     `json:"Name"`
	Title       string     `json:"Title"`
	Description string     `json:"Description"`
	Items       []ViewItem `json:"Items"`
}

type ViewItem struct {
	Name    string `json:"Name"`
	Type    string `json:"Type"`
	Title   string `json:"Title"`
	Value   string `json:"Value"`
	Default string `json:"Default"`
}

// Get the value of the ViewItem. If it is not set (""), return the default instead.
func (vi ViewItem) Get() string {
	if vi.Value == "" {
		logging.DebugLF("Returning default value for config item " + vi.Name)
		return vi.Default
	}
	return vi.Value
}

// AppConfigView gets the config with default values and other data replicated keeps
// CMD: replicatedctl app-config view
func (r *ReplicatedCtl) AppConfigView() (AppView, error) {
	av := AppView{viewItems: make(map[string]ViewItem)}
	raw, err := r.run("app-config", "view")
	if err != nil {
		return av, fmt.Errorf("failed to view app config from replicated: %w", err)
	}
	//var entries []ViewEntry
	err = json.Unmarshal(raw, &av)
	if err != nil {
		return av, fmt.Errorf("failed to unmarshall app config view: %w", err)
	}
	//av.ViewEntries = entries
	return av, err
}
