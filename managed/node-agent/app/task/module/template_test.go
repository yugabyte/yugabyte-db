// Copyright (c) YugabyteDB, Inc.

package module

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func writeTestTemplate(t *testing.T, content string) string {
	testFile, err := os.CreateTemp("/tmp", "*test.j2")
	if err != nil {
		t.Fatalf("Failed to create temp directory: %v", err)
	}
	defer testFile.Close()
	err = os.WriteFile(testFile.Name(), []byte(content), 0644)
	if err != nil {
		t.Fatalf("Failed to write to temp file: %v", err)
	}
	return testFile.Name()
}

func TestServerTemplate(t *testing.T) {
	gflags := map[string]string{
		"key1": "value1",
		"key2": "value2",
		"key3": "value3",
	}
	gflagsContext := map[string]any{
		"gflags": gflags,
	}
	projectDir := os.Getenv("PROJECT_DIR")
	templatePath := filepath.Join(projectDir, "resources/templates/server/yb-server-gflags.j2")
	output, err := ResolveTemplate(
		context.TODO(),
		gflagsContext,
		templatePath,
	)
	if err != nil {
		t.Fatalf("Failed to copy file: %v", err)
	}
	t.Logf("Output: %s", output)
}

func TestSplitString(t *testing.T) {
	values := map[string]any{
		"servers": "s1,s2,s3",
	}
	content := "servers is {{ servers | split_string }}"
	filename := writeTestTemplate(t, content)
	defer os.Remove(filename)
	output, err := ResolveTemplate(
		context.TODO(),
		values,
		filename,
	)
	if err != nil {
		t.Fatalf("Failed to copy file: %v", err)
	}
	expectedOutput := "servers is ['s1', 's2', 's3']"
	if output != expectedOutput {
		t.Fatalf("Unexpected output: %s, found %s", expectedOutput, output)
	}
	t.Logf("Output: %s", output)
}

func TestCustomBooleanTestFunc(t *testing.T) {
	testValues := []struct {
		Input          map[string]any
		ExpectedOutput string
	}{
		{map[string]any{"input": "true"}, " TRUE "},
		{map[string]any{"input": "false"}, " FALSE "},
		{map[string]any{"input": true}, " TRUE "},
		{map[string]any{"input": false}, " FALSE "},
	}
	content := "{% if input is true %} TRUE {% else %} FALSE {% endif %}"
	filename := writeTestTemplate(t, content)
	defer os.Remove(filename)
	for _, value := range testValues {
		output, err := ResolveTemplate(
			context.TODO(),
			value.Input,
			filename,
		)
		if err != nil {
			t.Fatalf("Failed to copy file: %v", err)
		}
		if output != value.ExpectedOutput {
			t.Fatalf("Unexpected output: %s, found %s", value.ExpectedOutput, output)
		}
		t.Logf("Output: %s", output)
	}
}

func TestConvertBoolString(t *testing.T) {
	testValues := []struct {
		Input          map[string]any
		ExpectedOutput string
	}{
		{map[string]any{"string_flag": "true"}, " TRUE "},
		{map[string]any{"string_flag": "false"}, " FALSE "},
	}
	content := "{% if string_flag | bool %} TRUE {% else %} FALSE {% endif %}"
	filename := writeTestTemplate(t, content)
	defer os.Remove(filename)
	for _, value := range testValues {
		output, err := ResolveTemplate(
			context.TODO(),
			value.Input,
			filename,
		)
		if err != nil {
			t.Fatalf("Failed to copy file: %v", err)
		}
		if output != value.ExpectedOutput {
			t.Fatalf("Unexpected output: %s, found %s", value.ExpectedOutput, output)
		}
		t.Logf("Output: %s", output)
	}
}
