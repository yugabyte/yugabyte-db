package ybactlstate

import (
	"slices"
	"testing"
)

func TestHandleMigrationAllRun(t *testing.T) {
	fs = mockFS{
		WriteBuffer: &devNullWriter{}, // we don't care about storing the state
	}
	schemaVersionCache = 8
	state := &State{}
	state._internalFields = internalFields{}
	state._internalFields.RunSchemas = make([]int, 0)
	migrations = make(map[int]migrator)
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
	migrations = make(map[int]migrator)
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
	migrations = make(map[int]migrator)
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
	migrations = make(map[int]migrator)
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
	migrations = make(map[int]migrator)
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
	migrations[index] = defaultMigrate
}
