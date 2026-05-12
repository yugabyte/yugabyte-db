// Copyright (c) YugabyteDB, Inc.

package model

import (
	"encoding/json"
	"reflect"
	"testing"
)

func TestMarshallUnmarshallWithUnknownFields(t *testing.T) {
	providerJson := `
{
  "code": "gcp",
  "name": "68bda3f756-20260212-100534",
  "details": {
    "cloudInfo": {
      "onprem": {
        "ybHomeDir": "/home/yugabyte",
		"unknownNestedFields": {"nestedField1": "nestedValue1","nestedField2": "nestedValue2"}
      }
    }
  },
  "unknownField": {"field1": "value1","field2": "value2"}
}`
	var provider Provider
	err := json.Unmarshal([]byte(providerJson), &provider)
	if err != nil {
		t.Fatalf("Failed to unmarshal provider JSON: %v", err)
	}
	// Validate a few known fields to ensure they are correctly marshalled.
	if provider.Code != "gcp" {
		t.Fatalf("Expected code to be 'gcp', found '%s'", provider.Code)
	}
	if provider.Name() != "68bda3f756-20260212-100534" {
		t.Fatalf("Expected name to be '68bda3f756-20260212-100534', found '%s'", provider.Name())
	}
	if provider.UnknownExtraFields == nil {
		t.Fatalf("Expected extra field to be non-nil")
	}
	var expectedUnknownFields = `{"field1": "value1","field2": "value2"}`
	var actualUnknownFields = string(provider.UnknownExtraFields["unknownField"])
	if actualUnknownFields != expectedUnknownFields {
		t.Fatalf(
			"Expected unknownField to be present in UnknownExtraFields. \nExpected: %s, \nFound: %s",
			expectedUnknownFields,
			actualUnknownFields,
		)
	}
	bytes, err := json.Marshal(provider)
	if err != nil {
		t.Fatalf("Failed to marshal provider JSON: %v", err)
	}
	t.Logf("Final JSON: %s\n", string(bytes))
	var finalMap map[string]interface{}
	err = json.Unmarshal(bytes, &finalMap)
	if err != nil {
		t.Fatalf("Failed to unmarshal final JSON: %v", err)
	}
	expectedFullMap := map[string]interface{}{}
	err = json.Unmarshal([]byte(providerJson), &expectedFullMap)
	if err != nil {
		t.Fatalf("Failed to unmarshal expected full JSON: %v", err)
	}
	// Check all the fields are present.
	if !reflect.DeepEqual(expectedFullMap, finalMap) {
		t.Fatalf(
			"Expected full JSON to match final JSON. \nExpected: %+v, \nFound: %+v",
			expectedFullMap,
			finalMap,
		)
	}
	if _, exists := finalMap["unknownField"]; !exists {
		t.Fatalf("Expected unknownField to be present in final JSON")
	}
	expectedMap := map[string]interface{}{
		"field1": "value1",
		"field2": "value2",
	}
	// Check all the unknown fields are present in the final JSON.
	if !reflect.DeepEqual(expectedMap, finalMap["unknownField"]) {
		t.Fatalf("Expected unknownField to have the correct value in final JSON")
	}
}

func TestMarshallUnmarshallWithoutUnknowns(t *testing.T) {
	providerJson := `
{
  "code": "gcp",
  "name": "68bda3f756-20260212-100534",
  "details": {
    "cloudInfo": {
      "onprem": {
        "ybHomeDir": "/home/yugabyte"
      }
    }
  }
}`
	var provider Provider
	err := json.Unmarshal([]byte(providerJson), &provider)
	if err != nil {
		t.Fatalf("Failed to unmarshal provider JSON: %v", err)
	}
	// Validate a few known fields to ensure they are correctly marshalled.
	if provider.Code != "gcp" {
		t.Fatalf("Expected code to be 'gcp', found '%s'", provider.Code)
	}
	if provider.Name() != "68bda3f756-20260212-100534" {
		t.Fatalf("Expected name to be '68bda3f756-20260212-100534', found '%s'", provider.Name())
	}
	if provider.UnknownExtraFields == nil {
		t.Fatalf("Expected extra field to be non-nil")
	}
}
