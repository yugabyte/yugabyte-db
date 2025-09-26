/*
 * Copyright (c) YugabyteDB, Inc.
 */

package release

import (
	"bytes"
	"encoding/json"
	"fmt"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (

	// Universe Details
	defaultUniverse = "table {{.Name}}\t{{.UUID}}\t{{.CreationDate}}"

	creationDateHeader = "Creation Date"
)

// UniverseContext for universe outputs
type UniverseContext struct {
	formatter.HeaderContext
	formatter.Context
	u ybaclient.Universe
}

// NewUniverseFormat for formatting output
func NewUniverseFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultUniverse
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetUniverse initializes the context with the universe data
func (u *UniverseContext) SetUniverse(universe ybaclient.Universe) {
	u.u = universe
}

type universeContext struct {
	Universe *UniverseContext
}

// Write populates the output table to be displayed in the command line
func (u *UniverseContext) Write(index int) error {
	var err error
	rc := &universeContext{
		Universe: &UniverseContext{},
	}
	rc.Universe.u = u.u

	// Section 1
	tmpl, err := u.startSubsection(defaultUniverse)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	u.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Universe %d: Details", index+1), formatter.BlueColor)))
	u.Output.Write([]byte("\n"))
	if err := u.ContextFormat(tmpl, rc.Universe); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	u.PostFormat(tmpl, NewUniverseContext())

	u.Output.Write([]byte("\n"))

	return nil
}

func (u *UniverseContext) startSubsection(format string) (*template.Template, error) {
	u.Buffer = bytes.NewBufferString("")
	u.ContextHeader = ""
	u.Format = formatter.Format(format)
	u.PreFormat()

	return u.ParseFormat()
}

// NewUniverseContext creates u new context for rendering universes
func NewUniverseContext() *UniverseContext {
	universeCtx := UniverseContext{}
	universeCtx.Header = formatter.SubHeaderContext{
		"Name":         formatter.NameHeader,
		"UUID":         formatter.UUIDHeader,
		"CreationDate": creationDateHeader,
	}
	return &universeCtx
}

// Name function
func (u *UniverseContext) Name() string {
	return u.u.GetName()
}

// UUID function
func (u *UniverseContext) UUID() string {
	return u.u.GetUuid()
}

// CreationDate function
func (u *UniverseContext) CreationDate() string {
	return util.PrintTime(u.u.GetCreationDate())
}

// MarshalJSON function
func (u *UniverseContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(u.u)
}
