package ybactlstate

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestStateJsonCarriesUnknownFields(t *testing.T) {
	loaded := `{"version":"2026.1.0.0-b1","config":{"hostname":"old","future_flag":true},` +
		`"future_section":{"id":12345678901234567890},` +
		`"__internal":{"change_id":1,"schema":1,"run_schemas":[1],"future_internal":"x"}}`
	state := &State{}
	if err := json.Unmarshal([]byte(loaded), state); err != nil {
		t.Fatalf("unmarshal failed: %s", err.Error())
	}
	state.Config.Hostname = "new"

	out, err := json.Marshal(state)
	if err != nil {
		t.Fatalf("marshal failed: %s", err.Error())
	}
	for _, want := range []string{
		`"hostname":"new"`,          // struct value wins over the loaded one
		`"as_root":false`,           // known fields are always written
		`"future_flag":true`,        // unknown nested key kept
		`"id":12345678901234567890`, // unknown value kept verbatim, not rounded
		`"future_internal":"x"`,     // unknown key inside __internal kept
		`"run_schemas":[1]`,         // internal fields still written
	} {
		if !strings.Contains(string(out), want) {
			t.Errorf("expected %s in %s", want, out)
		}
	}
	if strings.Contains(string(out), `"hostname":"old"`) {
		t.Errorf("loaded value must not override the struct: %s", out)
	}
}

func TestStateJsonWithoutLoadedDocument(t *testing.T) {
	out, err := json.Marshal(&State{Version: "v"})
	if err != nil {
		t.Fatalf("marshal failed: %s", err.Error())
	}
	if !strings.Contains(string(out), `"__internal"`) || !strings.Contains(string(out), `"version":"v"`) {
		t.Errorf("unexpected output %s", out)
	}
}
