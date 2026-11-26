package ybactlstate

import (
	"bytes"
	"encoding/json"
	"fmt"
)

type _stateAlias State

type _stateEncoder struct {
	_stateAlias
	Internal internalFields `json:"__internal"`
}

// UnmarshalJSON into state struct. Allows un-exported internal fields and keeps the loaded
// document for MarshalJSON and the migrations.
func (s *State) UnmarshalJSON(data []byte) error {
	var state _stateAlias
	if err := json.Unmarshal(data, &state); err != nil {
		return err
	}
	*s = State(state)
	t, err := unmarshalDocument(data)
	if err != nil {
		return err
	}
	internal, ok := t["__internal"]
	if !ok {
		return fmt.Errorf("invalid state, no __internal field")
	}
	iData, err := json.Marshal(internal)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(iData, &s._internalFields); err != nil {
		return err
	}
	s._loadedFields = t
	return nil
}

// MarshalJSON out of state struct, allows un-exported internal fields. Keys of the loaded
// document that this binary does not know are carried through, so a mix of yba-ctl versions
// rewriting the same state file cannot drop fields.
func (s State) MarshalJSON() ([]byte, error) {
	data, err := json.Marshal(_stateEncoder{_stateAlias(s), s._internalFields})
	if err != nil || s._loadedFields == nil {
		return data, err
	}
	doc, err := unmarshalDocument(data)
	if err != nil {
		return nil, err
	}
	carryUnknownFields(doc, s._loadedFields)
	return json.Marshal(doc)
}

// unmarshalDocument decodes json into nested maps, keeping numbers verbatim so values that are
// only carried through are not rounded to float64.
func unmarshalDocument(data []byte) (map[string]interface{}, error) {
	var doc map[string]interface{}
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	if err := dec.Decode(&doc); err != nil {
		return nil, err
	}
	return doc, nil
}

// carryUnknownFields copies into doc every key of loaded that doc does not have, recursing into
// objects present on both sides. Values doc already has always win.
func carryUnknownFields(doc, loaded map[string]interface{}) {
	for key, loadedVal := range loaded {
		docVal, ok := doc[key]
		if !ok {
			doc[key] = loadedVal
			continue
		}
		docObj, docIsObj := docVal.(map[string]interface{})
		loadedObj, loadedIsObj := loadedVal.(map[string]interface{})
		if docIsObj && loadedIsObj {
			carryUnknownFields(docObj, loadedObj)
		}
	}
}

func hasJsonPath(doc map[string]interface{}, path []string) bool {
	var cur interface{} = doc
	for _, key := range path {
		obj, ok := cur.(map[string]interface{})
		if !ok {
			return false
		}
		if cur, ok = obj[key]; !ok {
			return false
		}
	}
	return true
}
