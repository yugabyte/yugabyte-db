package ybactlstate

import (
	"bytes"
	"io"
	"slices"
	"strings"
	"testing"

	"github.com/spf13/viper"
)

type mockFilesystem struct {
	OpenErr   error
	CreateErr error

	OpenBuffer   *bytes.Buffer
	CreateBuffer *bytes.Buffer
}

func (m mockFilesystem) Open(fp string) (io.Reader, error) {
	return m.OpenBuffer, m.OpenErr
}

func (m mockFilesystem) Create(fp string) (io.Writer, error) {
	return m.CreateBuffer, m.CreateErr
}

// Returns a reset function that should be defered
func patchFS() (*mockFilesystem, func()) {
	old_fs := fs
	ms := &mockFilesystem{}
	fs = ms
	return ms, func() { fs = old_fs }
}

// State as rewritten by a 2025.2 yba-ctl after a 2026.1 preflight had already run migration 13:
// the migration is marked as run but config.as_root is gone.
const lostAsRootState = `{"version":"2025.2.5.2-b5","config":{"hostname":"192.0.2.10","self_signed_cert":true},` +
	`"services":{"yb-perf-advisor":false,"yb-platform":true},` +
	`"__internal":{"change_id":7,"schema":12,"run_schemas":[2,3,4,5,6,7,8,9,10,11,12,13]}}`

func TestLoadStateRerunsLostAsRootMigration(t *testing.T) {
	mockFS, deferFunc := patchFS()
	defer deferFunc()
	migrations = map[int]migration{
		stateServices: realMigrations[stateServices],
		asRootState:   realMigrations[asRootState],
	}
	schemaVersionCache = asRootState
	viper.Set("as_root", true)
	defer viper.Reset()
	mockFS.OpenBuffer = bytes.NewBufferString(lostAsRootState)
	mockFS.CreateBuffer = new(bytes.Buffer)

	state, err := LoadState()
	if err != nil {
		t.Fatalf("LoadState failed: %s", err.Error())
	}
	if !state.Config.AsRoot {
		t.Errorf("expected as_root to be re-derived from the config")
	}
	if !slices.Contains(state._internalFields.RunSchemas, asRootState) {
		t.Errorf("expected migration %d to be marked as run: %v", asRootState, state._internalFields.RunSchemas)
	}
	if stored := mockFS.CreateBuffer.String(); !strings.Contains(stored, `"as_root":true`) {
		t.Errorf("expected repaired state to be stored, found %s", stored)
	}
}

func TestLoadStateKeepsStoredAsRoot(t *testing.T) {
	mockFS, deferFunc := patchFS()
	defer deferFunc()
	migrations = map[int]migration{
		stateServices: realMigrations[stateServices],
		asRootState:   realMigrations[asRootState],
	}
	schemaVersionCache = asRootState
	viper.Set("as_root", true)
	defer viper.Reset()
	mockFS.OpenBuffer = bytes.NewBufferString(strings.Replace(lostAsRootState,
		`"self_signed_cert":true`, `"self_signed_cert":true,"as_root":false`, 1))
	mockFS.CreateBuffer = new(bytes.Buffer)

	state, err := LoadState()
	if err != nil {
		t.Fatalf("LoadState failed: %s", err.Error())
	}
	// A stored false is a real value, so ValidateReconfig can still catch a change to as_root
	if state.Config.AsRoot {
		t.Errorf("expected stored as_root to be kept")
	}
	if stored := mockFS.CreateBuffer.String(); stored != "" {
		t.Errorf("expected no migrations to run, found stored state %s", stored)
	}
}
