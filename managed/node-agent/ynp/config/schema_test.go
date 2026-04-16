// Copyright (c) YugabyteDB, Inc.

package config

import (
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"gopkg.in/yaml.v3"
)

func parseValidateConfig(t *testing.T, data string) error {
	generated := make(map[string]any)
	err := yaml.Unmarshal([]byte(data), &generated)
	if err != nil {
		t.Fatalf("Failed to unmarshal generated template YAML: %v", err)
	}
	return validate(t, generated, "")
}

func validate(t *testing.T, input any, prefix string) error {
	tp := reflect.TypeOf(input)
	val := reflect.ValueOf(input)
	if tp.Kind() == reflect.Map {
		for _, key := range val.MapKeys() {
			prefix := KeyPrefix(prefix, key.String())
			err := validate(t, val.MapIndex(key).Interface(), prefix)
			if err != nil {
				return err
			}
		}
		return nil
	}
	expected := fmt.Sprintf("__OPEN__ %s __CLOSE__", prefix)
	if !reflect.DeepEqual(expected, input) {
		t.Fatalf("Value mismatch for key %s: expected %s, got %v", prefix, expected, input)
	} else {
		t.Logf("Found correct %s\n", input)
	}
	return nil
}

// This generates the jinja template from the schema and validates.
func TestGenerateYnpTemplateYAML(t *testing.T) {
	ynpBaseDir := filepath.Join(os.Getenv("PROJECT_DIR"), "resources/ynp")
	schemaFilepath := filepath.Join(ynpBaseDir, YnpConfigSchemaPath)
	schemaHandler, err := NewSchemaHandler(schemaFilepath)
	if err != nil {
		t.Fatalf("Failed to create schema handler: %v", err)
	}
	templateFile, err := os.CreateTemp("/tmp", "ynp-*.yml")
	if err != nil {
		t.Fatalf("Failed to create temp file for generated config: %v", err)
	}
	templateFile.Close()
	defer os.Remove(templateFile.Name())
	str, err := schemaHandler.generateTemplateYAML()
	if err != nil {
		t.Fatalf("Failed to generate template YAML: %v", err)
	}
	parseValidateConfig(t, str)
}
