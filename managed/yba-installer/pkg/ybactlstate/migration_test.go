package ybactlstate

import (
	"encoding/json"
	"maps"
	"slices"
	"testing"
)

// The real migration table, captured before any test swaps it out.
var realMigrations = migrations

func TestHandleMigrationAllRun(t *testing.T) {
	fs = mockFS{
		WriteBuffer: &devNullWriter{}, // we don't care about storing the state
	}
	schemaVersionCache = 8
	state := &State{}
	state._internalFields = internalFields{}
	state._internalFields.RunSchemas = make([]int, 0)
	migrations = make(map[int]migration)
	for i := range 8 {
		addToMigrationMap(i + 1)
	}

	err := handleMigration(state)

	if err != nil {
		t.Errorf("running migrations failed: %s", err.Error())
	}
	expected := expectedRunSchemas(8)
	if slices.Compare(expected, state._internalFields.RunSchemas) != 0 {
		t.Errorf("expected slice %v. Found slice %v", expected, state._internalFields.RunSchemas)
	}
}

func TestHandleMigrationsPartialFullRun(t *testing.T) {
	fs = mockFS{
		WriteBuffer: &devNullWriter{}, // we don't care about storing the state
	}
	schemaVersionCache = 8
	state := &State{}
	state._internalFields = internalFields{}
	prev := []int{1, 2, 3, 4}
	state._internalFields.RunSchemas = prev
	migrations = make(map[int]migration)
	for i := range 8 {
		addToMigrationMap(i + 1)
	}

	err := handleMigration(state)

	if err != nil {
		t.Errorf("running migrations failed: %s", err.Error())
	}
	expected := expectedRunSchemas(8)
	if slices.Compare(expected, state._internalFields.RunSchemas) != 0 {
		t.Errorf("expected slice %v. Found slice %v", expected, state._internalFields.RunSchemas)
	}
}

func TestHandleMigrationsSkippedMigration(t *testing.T) {
	fs = mockFS{
		WriteBuffer: &devNullWriter{}, // we don't care about storing the state
	}
	schemaVersionCache = 8
	state := &State{}
	state._internalFields = internalFields{}
	prev := []int{1, 2, 3, 4, 6, 7, 8}
	state._internalFields.RunSchemas = prev
	migrations = make(map[int]migration)
	for i := range 8 {
		addToMigrationMap(i)
	}

	err := handleMigration(state)

	if err != nil {
		t.Errorf("running migrations failed: %s", err.Error())
	}
	// Custom order, as the missing schema should be run last
	expected := []int{1, 2, 3, 4, 6, 7, 8, 5}
	if slices.Compare(expected, state._internalFields.RunSchemas) != 0 {
		t.Errorf("expected slice %v. Found slice %v", expected, state._internalFields.RunSchemas)
	}
}

func TestHandleMigrationTransition(t *testing.T) {
	fs = mockFS{
		WriteBuffer: &devNullWriter{}, // we don't care about storing the state
	}
	schemaVersionCache = 8
	state := &State{}
	state._internalFields = internalFields{}
	state._internalFields.SchemaVersion = 6
	migrations = make(map[int]migration)
	for i := range 8 {
		addToMigrationMap(i + 1)
	}

	err := handleMigration(state)

	if err != nil {
		t.Errorf("running migrations failed: %s", err.Error())
	}
	expected := expectedRunSchemas(8)
	if slices.Compare(expected, state._internalFields.RunSchemas) != 0 {
		t.Errorf("expected slice %v. Found slice %v", expected, state._internalFields.RunSchemas)
	}
}

func TestNoMigrationDefinedForIndex(t *testing.T) {
	fs = mockFS{
		WriteBuffer: &devNullWriter{}, // we don't care about storing the state
	}
	schemaVersionCache = 8
	state := &State{}
	state._internalFields = internalFields{}
	// Skipping definition of migration 5
	migrations = make(map[int]migration)
	for _, v := range []int{1, 2, 3, 4, 6, 7, 8} {
		addToMigrationMap(v)
	}

	err := handleMigration(state)

	if err != nil {
		t.Errorf("running migrations failed: %s", err.Error())
	}
	// Custom order, as the missing schema should be run last
	expected := []int{1, 2, 3, 4, 6, 7, 8}
	if slices.Compare(expected, state._internalFields.RunSchemas) != 0 {
		t.Errorf("expected slice %v. Found slice %v", expected, state._internalFields.RunSchemas)
	}
}

func TestUpdateTrackingWithSkippedMigration(t *testing.T) {
	fs = mockFS{
		WriteBuffer: &devNullWriter{}, // we don't care about storing the state
	}
	schemaVersionCache = 8
	state := &State{}
	state._internalFields = internalFields{}
	state._internalFields.SchemaVersion = 6
	state._internalFields.RunSchemas = nil
	migrations = make(map[int]migration)
	for _, i := range []int{2, 3, 4, 5, 6, 8, 9} {
		addToMigrationMap(i)
	}
	err := updateSchemaTracking(state)
	if err != nil {
		t.Errorf("running migrations failed: %s", err.Error())
	}
	expected := []int{2, 3, 4, 5, 6}
	if slices.Compare(expected, state._internalFields.RunSchemas) != 0 {
		t.Errorf("expected slice %v. Found slice %v", expected, state._internalFields.RunSchemas)
	}
}

func expectedRunSchemas(v int) []int {
	expected := make([]int, v)
	start := 1
	for i := range v {
		expected[i] = start
		start++
	}
	return expected
}

func addToMigrationMap(index int) {
	migrations[index] = migration{run: defaultMigrate}
}

func TestHandleMigrationRerunsLostStateField(t *testing.T) {
	fs = mockFS{
		WriteBuffer: &devNullWriter{}, // we don't care about storing the state
	}
	tests := []struct {
		name   string
		loaded string      // state file content, empty for a state that was not loaded from a file
		runs   map[int]int // migrations expected to run, and how often
	}{
		{"fields present",
			`{"config":{"as_root":true},"services":{},"__internal":{"run_schemas":[1,2,3]}}`,
			map[int]int{}},
		{"nested field lost",
			`{"config":{"hostname":"h"},"services":{},"__internal":{"run_schemas":[1,2,3]}}`,
			map[int]int{1: 1}},
		{"top level field lost",
			`{"config":{"as_root":true},"__internal":{"run_schemas":[1,2,3]}}`,
			map[int]int{2: 1}},
		{"not run yet is not a loss",
			`{"config":{},"__internal":{"run_schemas":[3]}}`,
			map[int]int{1: 1, 2: 1}},
		{"not loaded from a file", "", map[int]int{}},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			runs := map[int]int{}
			counting := func(schema int) migrator {
				return func(*State) error { runs[schema]++; return nil }
			}
			migrations = map[int]migration{
				1: {run: counting(1), stateField: []string{"config", "as_root"}},
				2: {run: counting(2), stateField: []string{"services"}},
				3: {run: counting(3)},
			}
			schemaVersionCache = 3
			state := &State{}
			if test.loaded == "" {
				state._internalFields.RunSchemas = []int{1, 2, 3}
			} else if err := json.Unmarshal([]byte(test.loaded), state); err != nil {
				t.Fatalf("bad test state: %s", err.Error())
			}

			if err := handleMigration(state); err != nil {
				t.Fatalf("running migrations failed: %s", err.Error())
			}
			if !maps.Equal(runs, test.runs) {
				t.Errorf("expected runs %v. Found runs %v", test.runs, runs)
			}
			for schema := range runs {
				if !slices.Contains(state._internalFields.RunSchemas, schema) {
					t.Errorf("expected %d to be marked as run: %v", schema, state._internalFields.RunSchemas)
				}
			}
		})
	}
}
