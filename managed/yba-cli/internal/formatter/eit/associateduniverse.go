/*
 * Copyright (c) YugaByte, Inc.
 */

package eit

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
	defaultUni = "table {{.Name}}\t{{.UUID}}\t{{.State}}"
)

// AssociatedUniverseContext for associatedUni outputs
type AssociatedUniverseContext struct {
	formatter.HeaderContext
	formatter.Context
	u ybaclient.UniverseDetailSubset
}

// NewAssociatedUniverseFormat for formatting output
func NewAssociatedUniverseFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultUni
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetAssociatedUniverse initializes the context with the associatedUni data
func (u *AssociatedUniverseContext) SetAssociatedUniverse(
	associatedUni ybaclient.UniverseDetailSubset,
) {
	u.u = associatedUni
}

type associatedUniverseContext struct {
	AssociatedUniverse *AssociatedUniverseContext
}

// Write populates the output table to be displayed in the command line
func (u *AssociatedUniverseContext) Write(index int) error {
	var err error
	rc := &associatedUniverseContext{
		AssociatedUniverse: &AssociatedUniverseContext{},
	}
	rc.AssociatedUniverse.u = u.u

	// Section 1
	tmpl, err := u.startSubsection(defaultUni)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	u.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Associated Universe %d: Details", index+1), formatter.BlueColor)))
	u.Output.Write([]byte("\n"))
	if err := u.ContextFormat(tmpl, rc.AssociatedUniverse); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	u.PostFormat(tmpl, NewAssociatedUniverseContext())

	return nil
}

func (u *AssociatedUniverseContext) startSubsection(format string) (*template.Template, error) {
	u.Buffer = bytes.NewBufferString("")
	u.ContextHeader = ""
	u.Format = formatter.Format(format)
	u.PreFormat()

	return u.ParseFormat()
}

// NewAssociatedUniverseContext creates a new context for rendering associatedUniverses
func NewAssociatedUniverseContext() *AssociatedUniverseContext {
	associatedUniverseCtx := AssociatedUniverseContext{}
	associatedUniverseCtx.Header = formatter.SubHeaderContext{
		"Name":  formatter.NameHeader,
		"UUID":  formatter.UUIDHeader,
		"State": formatter.StateHeader,
	}
	return &associatedUniverseCtx
}

// UUID fetches AssociatedUniverse UUID
func (u *AssociatedUniverseContext) UUID() string {
	return u.u.GetUuid()
}

// Name fetches AssociatedUniverse Name
func (u *AssociatedUniverseContext) Name() string {
	return u.u.GetName()
}

// State fetches AssociatedUniverse State
func (u *AssociatedUniverseContext) State() string {

	updateInProgress := u.u.GetUpdateInProgress()
	universePaused := u.u.GetUniversePaused()

	allUpdatesSucceeded := u.u.GetUpdateSucceeded()

	if !updateInProgress && allUpdatesSucceeded && !universePaused {
		return formatter.Colorize(util.ReadyUniverseState, formatter.GreenColor)
	}
	if !updateInProgress && allUpdatesSucceeded && universePaused {
		return formatter.Colorize(util.PausedUniverseState, formatter.YellowColor)
	}
	if updateInProgress {
		return formatter.Colorize(util.PendingUniverseState, formatter.YellowColor)
	}
	if !updateInProgress && !allUpdatesSucceeded {

		formatter.Colorize(util.BadUniverseState, formatter.RedColor)

	}
	return formatter.Colorize(util.UnknownUniverseState, formatter.RedColor)
}

// MarshalJSON function
func (u *AssociatedUniverseContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(u.u)
}
