// Copyright (c) YugabyteDB, Inc.

package model

import (
	"encoding/json"
	"errors"
	"reflect"
)

// Extra is a struct that captures any unknown fields during JSON unmarshalling.
const (
	UnknownExtraFieldsName = "UnknownExtraFields"
)

// Extra is to capture any unknown fields into UnknownExtraFields during unmarshalling.
// It also marshals the struct back to JSON, including any unknown fields from UnknownExtraFields.
// The purpose is not to lose any unknown fields during API calls when only interested fields are defined in the struct.
type Extra struct {
	UnknownExtraFields map[string]json.RawMessage `json:"-"`
}

// findKnownFields marshals the struct to JSON and unmarshals it back to a map to get the known fields as JSON.RawMessage.
func findKnownFields(in any) (map[string]json.RawMessage, error) {
	knownJson, err := json.Marshal(in)
	if err != nil {
		return nil, err
	}
	// Unmarshal known fields into a map to combine with unknown fields.
	// RawMessage is used to avoid losing type and also faster.
	var knownRaw map[string]json.RawMessage
	if err := json.Unmarshal(knownJson, &knownRaw); err != nil {
		return nil, err
	}
	return knownRaw, nil
}

// marshalUnknownExtraFields marshals the struct back to JSON, including any unknown fields from UnknownExtraFields.
func marshalUnknownExtraFields(in any) ([]byte, error) {
	rv := reflect.ValueOf(in)
	if rv.Kind() != reflect.Pointer || rv.IsNil() {
		return nil, errors.New(
			"Invalid receiver to marshalUnknownFields: must be a non-nil pointer",
		)
	}
	knownFields, err := findKnownFields(in)
	if err != nil {
		return nil, err
	}
	field := rv.Elem().FieldByName(UnknownExtraFieldsName)
	if field.IsValid() && !field.IsNil() && !field.IsZero() {
		// Capture unknown fields from the struct and combine with known fields for final marshalling.
		if unknownFields, ok := field.Interface().(map[string]json.RawMessage); ok {
			for key, val := range unknownFields {
				// Avoid overwriting any known fields with unknown fields in case of key collision.
				if _, exists := knownFields[key]; !exists {
					knownFields[key] = val
				}
			}
		}
	}
	return json.Marshal(knownFields)
}

// unmarshalUnknownExtraFields unmarshals the input JSON into the struct and captures any unknown fields into UnknownExtraFields.
func unmarshalUnknownExtraFields(data []byte, typeAlias any) error {
	rv := reflect.ValueOf(typeAlias)
	if rv.Kind() != reflect.Pointer || rv.IsNil() {
		return errors.New(
			"Invalid receiver to unmarshalUnknownExtraFields: must be a non-nil pointer",
		)
	}
	// Unmarshal into the known struct fields.
	if err := json.Unmarshal(data, typeAlias); err != nil {
		return err
	}
	field := rv.Elem().FieldByName(UnknownExtraFieldsName)
	if field.IsValid() && field.CanSet() {
		// Get the known fields as JSON.
		knownFields, err := findKnownFields(typeAlias)
		if err != nil {
			return err
		}
		var raw map[string]json.RawMessage
		if err := json.Unmarshal(data, &raw); err != nil {
			return err
		}
		// Delete known fields from the raw map to get only unknown fields, and set it to the struct field.
		for key := range knownFields {
			delete(raw, key)
		}
		field.Set(reflect.ValueOf(raw))
	}
	return nil
}
