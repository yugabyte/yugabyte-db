/*
 * Copyright (c) YugaByte, Inc.
 */

package gflags

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultGflags = "table {{.SpecificGFlags}}"

	specificGFlagsHeader = "Specific GFlags (in json)"
)

// Context for universe outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	g util.SpecificGFlagsCLIOutput
}

// NewGFlagsUniverseFormat for formatting output
func NewGFlagsUniverseFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultGflags
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Universes
func Write(ctx formatter.Context, gflags []util.SpecificGFlagsCLIOutput) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of universes into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(gflags, "", "  ")
		} else {
			output, err = json.Marshal(gflags)
		}

		if err != nil {
			logrus.Errorf("Error marshaling universe gflags to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}

	// Existing logic for table and other formats
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, gflag := range gflags {
			err := format(&Context{g: gflag})
			if err != nil {
				logrus.Debugf("Error rendering universe gflag: %v\n", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewGFlagsUniverseContext(), render)
}

// NewGFlagsUniverseContext creates a new context for rendering universe
func NewGFlagsUniverseContext() *Context {
	universeCtx := Context{}
	universeCtx.Header = formatter.SubHeaderContext{
		"SpecificGFlags": specificGFlagsHeader,
	}
	return &universeCtx
}

// SpecificGFlags for formatting output
func (c *Context) SpecificGFlags() string {
	jsonBytes, err := json.MarshalIndent(c.g, "", "  ")
	if err != nil {
		logrus.Error("Error converting JSON to string:", err)
		return ""
	}
	return string(jsonBytes)
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.g)
}
