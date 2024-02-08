/*
 * Copyright (c) YugaByte, Inc.
 */

package formatter

import "strings"

// SubContext defines what Context implementation should provide
type SubContext interface {
	FullHeader() interface{}
}

// SubHeaderContext is a map destined to formatter header (table format)
type SubHeaderContext map[string]string

// Label returns the header label for the specified string
func (c SubHeaderContext) Label(name string) string {
	n := strings.Split(name, ".")
	r := strings.NewReplacer("-", " ", "_", " ")
	h := r.Replace(n[len(n)-1])

	return h
}

// HeaderContext provides the subContext interface for managing headers
type HeaderContext struct {
	Header interface{}
}

// FullHeader returns the header as an interface
func (c *HeaderContext) FullHeader() interface{} {
	return c.Header
}
