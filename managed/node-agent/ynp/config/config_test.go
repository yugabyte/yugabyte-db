// Copyright (c) YugabyteDB, Inc.

package config

import (
	"reflect"
	"testing"
)

func TestFixParsedConfigMapNilValues(t *testing.T) {
	input := map[string]map[string]any{
		"section": {
			"optional_field": nil,
			"list_with_nil":  []any{1, nil, 3},
			"nested": map[interface{}]interface{}{
				"inner_nil": nil,
			},
		},
		"empty_section": {},
	}

	result := FixParsedConfigMap(input)

	expected := map[string]map[string]any{
		"section": {
			"optional_field": nil,
			"list_with_nil":  []any{1, nil, 3},
			"nested": map[string]any{
				"inner_nil": nil,
			},
		},
		"empty_section": {},
	}

	if !reflect.DeepEqual(expected, result) {
		t.Fatalf("FixParsedConfigMap() = %#v, want %#v", result, expected)
	}
}
