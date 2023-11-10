/*
 * Copyright (c) YugaByte, Inc.
 */

package util

import "encoding/json"

// StringListListFlag describes a list of list of strings
type StringListListFlag [][]string

// Type function describes the custom defined type
func (s *StringListListFlag) Type() string {
	return "stringListList"
}

// String returns a string version of the JSON list of lists
func (s *StringListListFlag) String() string {
	strListListJSON, _ := json.Marshal(s)
	return string(strListListJSON)
}

// Set value adds the value string to a list of list of string
func (s *StringListListFlag) Set(value string) error {
	var strListList [][]string
	err := json.Unmarshal([]byte(value), &strListList)
	if err != nil {
		return err
	}
	*s = StringListListFlag(strListList)
	return nil
}
