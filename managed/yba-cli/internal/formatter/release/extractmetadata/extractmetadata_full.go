/*
 * Copyright (c) YugabyteDB, Inc.
 */

package extractmetadata

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// ExtractMetadata details
	defaultFullExtractMetadataGeneral = "table {{.Version}}\t{{.UUID}}\t{{.ReleaseType}}" +
		"\t{{.Architecture}}\t{{.ReleaseDate}}\t{{.State}}"
	releaseDetails1 = "table {{.SHA256}}\t{{.YbType}}"
	releaseDetails2 = "table {{.ReleaseNotes}}"
)

// FullExtractMetadataContext to render ExtractMetadata Details output
type FullExtractMetadataContext struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.ResponseExtractMetadata
}

// SetFullExtractMetadata initializes the context with the release data
func (fr *FullExtractMetadataContext) SetFullExtractMetadata(
	release ybaclient.ResponseExtractMetadata,
) {
	fr.r = release
}

// NewFullExtractMetadataFormat for formatting output
func NewFullExtractMetadataFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultExtractMetadataListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullExtractMetadataContext struct {
	ExtractMetadata *Context
}

// Write populates the output table to be displayed in the command line
func (fr *FullExtractMetadataContext) Write() error {
	var err error
	frc := &fullExtractMetadataContext{
		ExtractMetadata: &Context{},
	}
	frc.ExtractMetadata.r = fr.r

	// Section 1
	tmpl, err := fr.startSubsection(defaultFullExtractMetadataGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
	if err := fr.ContextFormat(tmpl, frc.ExtractMetadata); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewExtractMetadataContext())
	fr.Output.Write([]byte("\n"))

	// Section 2: ExtractMetadata Details subSection 1
	tmpl, err = fr.startSubsection(releaseDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.subSection("Extract Metadata Details")
	if err := fr.ContextFormat(tmpl, frc.ExtractMetadata); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewExtractMetadataContext())
	fr.Output.Write([]byte("\n"))

	// Section 2: ExtractMetadata Details subSection 2
	tmpl, err = fr.startSubsection(releaseDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fr.ContextFormat(tmpl, frc.ExtractMetadata); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewExtractMetadataContext())
	fr.Output.Write([]byte("\n"))

	return nil
}

func (fr *FullExtractMetadataContext) startSubsection(format string) (*template.Template, error) {
	fr.Buffer = bytes.NewBufferString("")
	fr.ContextHeader = ""
	fr.Format = formatter.Format(format)
	fr.PreFormat()

	return fr.ParseFormat()
}

func (fr *FullExtractMetadataContext) subSection(name string) {
	fr.Output.Write([]byte("\n"))
	fr.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
}

// NewFullExtractMetadataContext creates a new context for rendering release
func NewFullExtractMetadataContext() *FullExtractMetadataContext {
	releaseCtx := FullExtractMetadataContext{}
	releaseCtx.Header = formatter.SubHeaderContext{}
	return &releaseCtx
}

// MarshalJSON function
func (fr *FullExtractMetadataContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fr.r)
}
