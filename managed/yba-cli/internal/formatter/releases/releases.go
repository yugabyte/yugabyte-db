/*
 * Copyright (c) YugaByte, Inc.
 */

package releases

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultReleasesListing = "table {{.Version}}\t{{.State}}"
	versionHeader          = "YugabyteDB Version"
)

// Context for releases outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	release map[string]interface{}
}

// NewReleasesFormat for formatting output
func NewReleasesFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultReleasesListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Releases
func Write(
	ctx formatter.Context,
	releases []map[string]interface{},
) error {
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, releaseMetadata := range releases {
			err := format(&Context{
				release: releaseMetadata,
			})

			if err != nil {
				logrus.Debugf("Error rendering releases: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewReleasesContext(), render)
}

// NewReleasesContext creates a new context for rendering releases
func NewReleasesContext() *Context {
	releasesCtx := Context{}
	releasesCtx.Header = formatter.SubHeaderContext{
		"Version": versionHeader,
		"State":   formatter.StateHeader,
	}
	return &releasesCtx
}

// Version of YugabyteDB release
func (c *Context) Version() string {
	version := c.release["version"].(string)
	return version
}

// State of YugabyteDB release
func (c *Context) State() string {
	state := c.release["state"].(string)
	return state
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.release)
}
