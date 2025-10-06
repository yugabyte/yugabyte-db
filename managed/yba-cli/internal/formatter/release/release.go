/*
 * Copyright (c) YugabyteDB, Inc.
 */

package release

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultReleaseListing = "table {{.Version}}\t{{.UUID}}\t{{.ReleaseType}}" +
		"\t{{.Architecture}}\t{{.ReleaseDate}}\t{{.State}}"

	versionHeader      = "YugabyteDB Version"
	artifactsHeader    = "Artifacts"
	releaseDateHeader  = "Release Date"
	releaseNotesHeader = "Release Notes"
	releaseTagHeader   = "Release Tag"
	releaseTypeHeader  = "Release Type"
	ybTypeHeader       = "YugabyteDB Type"
	architectureHeader = "Architecture"
)

// Context for releases outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.ResponseRelease
}

// NewReleaseFormat for formatting output
func NewReleaseFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultReleaseListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Release
func Write(
	ctx formatter.Context,
	releases []ybaclient.ResponseRelease,
) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of releases into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(releases, "", "  ")
		} else {
			output, err = json.Marshal(releases)
		}

		if err != nil {
			logrus.Errorf("Error marshaling releases to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}

	// Existing logic for table and other formats
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, releaseMetadata := range releases {
			err := format(&Context{
				r: releaseMetadata,
			})

			if err != nil {
				logrus.Debugf("Error rendering releases: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewReleaseContext(), render)
}

// NewReleaseContext creates a new context for rendering releases
func NewReleaseContext() *Context {
	releasesCtx := Context{}
	releasesCtx.Header = formatter.SubHeaderContext{
		"Version":      versionHeader,
		"State":        formatter.StateHeader,
		"Architecture": architectureHeader,
		"Artifacts":    artifactsHeader,
		"ReleaseDate":  releaseDateHeader,
		"ReleaseNotes": releaseNotesHeader,
		"ReleaseTag":   releaseTagHeader,
		"ReleaseType":  releaseTypeHeader,
		"UUID":         formatter.UUIDHeader,
		"Universes":    formatter.UniversesHeader,
		"YbType":       ybTypeHeader,
	}
	return &releasesCtx
}

// Version of YugabyteDB release
func (c *Context) Version() string {
	return c.r.GetVersion()
}

// State of YugabyteDB release
func (c *Context) State() string {
	state := c.r.GetState()
	if strings.Compare(state, "ACTIVE") == 0 {
		return formatter.Colorize(state, formatter.GreenColor)
	}
	return formatter.Colorize(state, formatter.YellowColor)
}

// UUID of YugabyteDB release
func (c *Context) UUID() string {
	return c.r.GetReleaseUuid()
}

// ReleaseType of YugabyteDB release
func (c *Context) ReleaseType() string {
	return c.r.GetReleaseType()
}

// Architecture of YugabyteDB release
func (c *Context) Architecture() string {
	arch := "-"
	for i, v := range c.r.GetArtifacts() {
		vArch := ""
		if len(strings.TrimSpace(v.GetArchitecture())) != 0 {
			vArch = v.GetArchitecture()
		} else {
			vArch = "kubernetes"
		}
		if i == 0 {
			arch = vArch
		} else {
			arch = fmt.Sprintf("%s, %s", arch, vArch)
		}
	}
	return arch
}

// Universes of YugabyteDB release
func (c *Context) Universes() string {
	universes := "-"
	for i, v := range c.r.GetUniverses() {
		if i == 0 {
			universes = v.GetName()
		} else {
			universes = fmt.Sprintf("%s, %s", universes, v.GetName())
		}
	}
	return universes
}

// YbType of YugabyteDB release
func (c *Context) YbType() string {
	return c.r.GetYbType()
}

// ReleaseDate of YugabyteDB release
func (c *Context) ReleaseDate() string {
	return util.PrintTime(util.FromEpochMilli(c.r.GetReleaseDateMsecs()))
}

// ReleaseTag of YugabyteDB release
func (c *Context) ReleaseTag() string {
	return c.r.GetReleaseTag()
}

// ReleaseNotes of YugabyteDB release
func (c *Context) ReleaseNotes() string {
	return c.r.GetReleaseNotes()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.r)
}
