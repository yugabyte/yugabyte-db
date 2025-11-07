/*
 * Copyright (c) YugabyteDB, Inc.
 */

package extractmetadata

import (
	"encoding/json"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultExtractMetadataListing = "table {{.Version}}\t{{.UUID}}\t{{.Platform}}{{.ReleaseType}}" +
		"\t{{.Architecture}}\t{{.ReleaseDate}}\t{{.State}}"

	versionHeader      = "YugabyteDB Version"
	uuidHeader         = "Metadata UUID"
	releaseDateHeader  = "Release Date"
	releaseNotesHeader = "Release Notes"
	sha256Header       = "SHA256"
	releaseTypeHeader  = "Release Type"
	ybTypeHeader       = "YugabyteDB Type"
	architectureHeader = "Architecture"
	platformHeader     = "Platform"
)

// Context for extractMetadatas outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.ResponseExtractMetadata
}

// NewExtractMetadataFormat for formatting output
func NewExtractMetadataFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultExtractMetadataListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of ExtractMetadata
func Write(
	ctx formatter.Context,
	extractMetadatas []ybaclient.ResponseExtractMetadata,
) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of extractMetadatas into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(extractMetadatas, "", "  ")
		} else {
			output, err = json.Marshal(extractMetadatas)
		}

		if err != nil {
			logrus.Errorf("Error marshaling extract metadatas to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}

	// Existing logic for table and other formats
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, extractMetadataMetadata := range extractMetadatas {
			err := format(&Context{
				r: extractMetadataMetadata,
			})

			if err != nil {
				logrus.Debugf("Error rendering extract metadatas: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewExtractMetadataContext(), render)
}

// NewExtractMetadataContext creates a new context for rendering extractMetadatas
func NewExtractMetadataContext() *Context {
	extractMetadatasCtx := Context{}
	extractMetadatasCtx.Header = formatter.SubHeaderContext{
		"Version":      versionHeader,
		"State":        formatter.StateHeader,
		"Architecture": architectureHeader,
		"ReleaseDate":  releaseDateHeader,
		"ReleaseNotes": releaseNotesHeader,
		"SHA256":       sha256Header,
		"ReleaseType":  releaseTypeHeader,
		"UUID":         uuidHeader,
		"Platform":     platformHeader,
		"YbType":       ybTypeHeader,
	}
	return &extractMetadatasCtx
}

// Version of YugabyteDB extractMetadata
func (c *Context) Version() string {
	return c.r.GetVersion()
}

// State of YugabyteDB extractMetadata
func (c *Context) State() string {
	state := c.r.GetStatus()
	if strings.Compare(state, util.SuccessReleaseResponseState) == 0 {
		return formatter.Colorize(state, formatter.GreenColor)
	}
	if strings.Compare(state, util.FailureReleaseResponseState) == 0 {
		return formatter.Colorize(state, formatter.RedColor)
	}
	return formatter.Colorize(state, formatter.YellowColor)
}

// UUID of YugabyteDB extractMetadata
func (c *Context) UUID() string {
	return c.r.GetMetadataUuid()
}

// ReleaseType of YugabyteDB extractMetadata
func (c *Context) ReleaseType() string {
	return c.r.GetReleaseType()
}

// Architecture of YugabyteDB release
func (c *Context) Architecture() string {
	return c.r.GetArchitecture()
}

// Platform of YugabyteDB release
func (c *Context) Platform() string {
	return c.r.GetPlatform()
}

// SHA256 of YugabyteDB release
func (c *Context) SHA256() string {
	return c.r.GetSha256()
}

// YbType of YugabyteDB release
func (c *Context) YbType() string {
	return c.r.GetYbType()
}

// ReleaseDate of YugabyteDB release
func (c *Context) ReleaseDate() string {
	return util.PrintTime(util.FromEpochMilli(c.r.GetReleaseDateMsecs()))
}

// ReleaseNotes of YugabyteDB release
func (c *Context) ReleaseNotes() string {
	return c.r.GetReleaseNotes()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.r)
}
