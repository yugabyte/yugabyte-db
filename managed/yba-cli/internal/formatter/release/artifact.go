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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (

	// Artifact Details
	defaultArtifact  = "table {{.Architecture}}\t{{.Platform}}"
	artifactDetails  = "table {{.PackageFileID}}\t{{.PackageURL}}"
	artifactMetadata = "table {{.Sha256}}"

	platformHeader      = "Platform"
	packageFileIDHeader = "Package File ID"
	packageURLHeader    = "Package URL"
	sha256Header        = "SHA256"
)

// ArtifactContext for artifact outputs
type ArtifactContext struct {
	formatter.HeaderContext
	formatter.Context
	a ybaclient.Artifact
}

// NewArtifactFormat for formatting output
func NewArtifactFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultArtifact
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetArtifact initializes the context with the artifact data
func (a *ArtifactContext) SetArtifact(artifact ybaclient.Artifact) {
	a.a = artifact
}

type artifactContext struct {
	Artifact *ArtifactContext
}

// Write populates the output table to be displayed in the command line
func (a *ArtifactContext) Write(index int) error {
	var err error
	rc := &artifactContext{
		Artifact: &ArtifactContext{},
	}
	rc.Artifact.a = a.a

	// Section 1
	tmpl, err := a.startSubsection(defaultArtifact)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	a.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Artifact %d: Details", index+1), formatter.BlueColor)))
	a.Output.Write([]byte("\n"))
	if err := a.ContextFormat(tmpl, rc.Artifact); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	a.PostFormat(tmpl, NewArtifactContext())

	a.Output.Write([]byte("\n"))

	// Section 2: artifact details
	tmpl, err = a.startSubsection(artifactDetails)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := a.ContextFormat(tmpl, rc.Artifact); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	a.PostFormat(tmpl, NewArtifactContext())
	a.Output.Write([]byte("\n"))

	// Section 4: artifact metadata
	tmpl, err = a.startSubsection(artifactMetadata)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := a.ContextFormat(tmpl, rc.Artifact); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	a.PostFormat(tmpl, NewArtifactContext())
	a.Output.Write([]byte("\n"))

	return nil
}

func (a *ArtifactContext) startSubsection(format string) (*template.Template, error) {
	a.Buffer = bytes.NewBufferString("")
	a.ContextHeader = ""
	a.Format = formatter.Format(format)
	a.PreFormat()

	return a.ParseFormat()
}

// NewArtifactContext creates a new context for rendering artifacts
func NewArtifactContext() *ArtifactContext {
	artifactCtx := ArtifactContext{}
	artifactCtx.Header = formatter.SubHeaderContext{
		"Architecture":  architectureHeader,
		"Platform":      platformHeader,
		"PackageFileID": packageFileIDHeader,
		"PackageURL":    packageURLHeader,
		"Sha256":        sha256Header,
	}
	return &artifactCtx
}

// Architecture of YugabyteDB release
func (a *ArtifactContext) Architecture() string {
	return a.a.GetArchitecture()
}

// Platform of YugabyteDB release
func (a *ArtifactContext) Platform() string {
	return a.a.GetPlatform()
}

// PackageFileID of YugabyteDB release
func (a *ArtifactContext) PackageFileID() string {
	return a.a.GetPackageFileId()
}

// PackageURL of YugabyteDB release
func (a *ArtifactContext) PackageURL() string {
	return a.a.GetPackageUrl()
}

// Sha256 of YugabyteDB release
func (a *ArtifactContext) Sha256() string {
	return a.a.GetSha256()
}

// MarshalJSON function
func (a *ArtifactContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(a.a)
}
