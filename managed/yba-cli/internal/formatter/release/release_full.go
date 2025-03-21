/*
 * Copyright (c) YugaByte, Inc.
 */

package release

import (
	"bytes"
	"encoding/json"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// Release details
	defaultFullReleaseGeneral = "table {{.Version}}\t{{.UUID}}\t{{.ReleaseType}}" +
		"\t{{.ReleaseDate}}\t{{.State}}"
	releaseDetails1 = "table {{.ReleaseTag}}\t{{.YbType}}"
	releaseDetails2 = "table {{.ReleaseNotes}}"

	// Access key Details

)

// FullReleaseContext to render Release Details output
type FullReleaseContext struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.ResponseRelease
}

// SetFullRelease initializes the context with the release data
func (fr *FullReleaseContext) SetFullRelease(release ybaclient.ResponseRelease) {
	fr.r = release
}

// NewFullReleaseFormat for formatting output
func NewFullReleaseFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultReleaseListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullReleaseContext struct {
	Release *Context
}

// Write populates the output table to be displayed in the command line
func (fr *FullReleaseContext) Write() error {
	var err error
	frc := &fullReleaseContext{
		Release: &Context{},
	}
	frc.Release.r = fr.r

	// Section 1
	tmpl, err := fr.startSubsection(defaultFullReleaseGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
	if err := fr.ContextFormat(tmpl, frc.Release); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewReleaseContext())
	fr.Output.Write([]byte("\n"))

	// Section 2: Release Details subSection 1
	tmpl, err = fr.startSubsection(releaseDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.subSection("Release Details")
	if err := fr.ContextFormat(tmpl, frc.Release); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewReleaseContext())
	fr.Output.Write([]byte("\n"))

	// Section 2: Release Details subSection 2
	tmpl, err = fr.startSubsection(releaseDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fr.ContextFormat(tmpl, frc.Release); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewReleaseContext())
	fr.Output.Write([]byte("\n"))

	// Artifacts subSection
	logrus.Debugf("Number of Artifacts: %d", len(fr.r.GetArtifacts()))
	fr.subSection("Artifacts")
	for i, v := range fr.r.GetArtifacts() {
		artifactContext := *NewArtifactContext()
		artifactContext.Output = os.Stdout
		artifactContext.Format = NewFullReleaseFormat(viper.GetString("output"))
		artifactContext.SetArtifact(v)
		artifactContext.Write(i)
	}

	// Universes
	noOfUniverses := len(fr.r.GetUniverses())
	if noOfUniverses > 0 {
		logrus.Debugf("Number of Universes: %d", noOfUniverses)
		fr.subSection("Universes")
		for i, v := range fr.r.GetUniverses() {
			universeContext := *NewUniverseContext()
			universeContext.Output = os.Stdout
			universeContext.Format = NewFullReleaseFormat(viper.GetString("output"))
			universeContext.SetUniverse(v)
			universeContext.Write(i)
		}
	}

	return nil
}

func (fr *FullReleaseContext) startSubsection(format string) (*template.Template, error) {
	fr.Buffer = bytes.NewBufferString("")
	fr.ContextHeader = ""
	fr.Format = formatter.Format(format)
	fr.PreFormat()

	return fr.ParseFormat()
}

func (fr *FullReleaseContext) subSection(name string) {
	fr.Output.Write([]byte("\n"))
	fr.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
}

// NewFullReleaseContext creates a new context for rendering release
func NewFullReleaseContext() *FullReleaseContext {
	releaseCtx := FullReleaseContext{}
	releaseCtx.Header = formatter.SubHeaderContext{}
	return &releaseCtx
}

// MarshalJSON function
func (fr *FullReleaseContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fr.r)
}
