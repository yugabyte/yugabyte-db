/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

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

	// ImageBundle Details
	defaultImageBundle = "table {{.Name}}\t{{.UUID}}\t{{.IsDefault}}\t{{.Active}}"
	imageBundleDetails = "table {{.GlobalYbImage}}\t{{.Arch}}\t" +
		"{{.SSHUser}}\t{{.SSHPort}}\t{{.UseIMDSv2}}"
	regionOverridesTable = "table {{.RegionOverrides}}"
	imageBundleMetadata  = "table {{.ImageBundleType}}\t{{.Version}}"

	isDefaultHeader       = "Is Default"
	activeHeader          = "Active"
	globalYbImageHeader   = "Global YB Image"
	archHeader            = "Architecture"
	useIMDSv2Header       = "Use IMDS V2"
	regionOverridesHeader = "Region Overrides"
	imageBundleTypeHeader = "Image Bundle Type"
	versionHeader         = "Version"
)

// ImageBundleContext for imageBundle outputs
type ImageBundleContext struct {
	formatter.HeaderContext
	formatter.Context
	ib ybaclient.ImageBundle
}

// NewImageBundleFormat for formatting output
func NewImageBundleFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultImageBundle
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetImageBundle initializes the context with the imageBundle data
func (ib *ImageBundleContext) SetImageBundle(imageBundle ybaclient.ImageBundle) {
	ib.ib = imageBundle
}

type imageBundleContext struct {
	ImageBundle *ImageBundleContext
}

// Write populates the output table to be displayed in the command line
func (ib *ImageBundleContext) Write(providerCode string, index int) error {
	var err error
	rc := &imageBundleContext{
		ImageBundle: &ImageBundleContext{},
	}
	rc.ImageBundle.ib = ib.ib

	// Section 1
	tmpl, err := ib.startSubsection(defaultImageBundle)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ib.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Linux Version %d: Details", index+1), formatter.BlueColor)))
	ib.Output.Write([]byte("\n"))
	if err := ib.ContextFormat(tmpl, rc.ImageBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ib.PostFormat(tmpl, NewImageBundleContext())

	ib.Output.Write([]byte("\n"))

	// Section 2: image bundle details
	tmpl, err = ib.startSubsection(imageBundleDetails)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ib.subSection("Image Bundle Details")
	if err := ib.ContextFormat(tmpl, rc.ImageBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ib.PostFormat(tmpl, NewImageBundleContext())
	ib.Output.Write([]byte("\n"))

	// Section 3: region overrides
	tmpl, err = ib.startSubsection(regionOverridesTable)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := ib.ContextFormat(tmpl, rc.ImageBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ib.PostFormat(tmpl, NewImageBundleContext())

	// Section 4: image bundle metadata
	tmpl, err = ib.startSubsection(imageBundleMetadata)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ib.subSection("Image Bundle Metadata")
	if err := ib.ContextFormat(tmpl, rc.ImageBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ib.PostFormat(tmpl, NewImageBundleContext())
	ib.Output.Write([]byte("\n"))

	return nil
}

func (ib *ImageBundleContext) startSubsection(format string) (*template.Template, error) {
	ib.Buffer = bytes.NewBufferString("")
	ib.ContextHeader = ""
	ib.Format = formatter.Format(format)
	ib.PreFormat()

	return ib.ParseFormat()
}

func (ib *ImageBundleContext) subSection(name string) {
	ib.Output.Write([]byte("\n\n"))
	ib.Output.Write([]byte(formatter.Colorize(name, formatter.BlueColor)))
	ib.Output.Write([]byte("\n"))
}

// NewImageBundleContext creates a new context for rendering imageBundles
func NewImageBundleContext() *ImageBundleContext {
	imageBundleCtx := ImageBundleContext{}
	imageBundleCtx.Header = formatter.SubHeaderContext{
		"Name":            formatter.NameHeader,
		"UUID":            formatter.UUIDHeader,
		"IsDefault":       isDefaultHeader,
		"Active":          activeHeader,
		"GlobalYbImage":   globalYbImageHeader,
		"Arch":            archHeader,
		"SSHPort":         sshPortHeader,
		"SSHUser":         sshUserHeader,
		"UseIMDSv2":       useIMDSv2Header,
		"RegionOverrides": regionOverridesHeader,
		"ImageBundleType": imageBundleTypeHeader,
		"Version":         versionHeader,
	}
	return &imageBundleCtx
}

// UUID fetches ImageBundle UUID
func (ib *ImageBundleContext) UUID() string {
	return ib.ib.GetUuid()
}

// Name fetches ImageBundle Name
func (ib *ImageBundleContext) Name() string {
	return ib.ib.GetName()
}

// IsDefault fetches ImageBundle IsDefault
func (ib *ImageBundleContext) IsDefault() string {
	return fmt.Sprintf("%t", ib.ib.GetUseAsDefault())
}

// Active fetches if ImageBundle is Active ImageBundle
func (ib *ImageBundleContext) Active() string {
	return fmt.Sprintf("%t", ib.ib.GetActive())
}

// GlobalYbImage fetches global image for the bundle
func (ib *ImageBundleContext) GlobalYbImage() string {
	details := ib.ib.GetDetails()
	return details.GetGlobalYbImage()
}

// Arch fetches architecture of the bundle
func (ib *ImageBundleContext) Arch() string {
	details := ib.ib.GetDetails()
	return details.GetArch()
}

// SSHPort fetches the ssh port defined for the bundle
func (ib *ImageBundleContext) SSHPort() string {
	details := ib.ib.GetDetails()
	return fmt.Sprintf("%d", details.GetSshPort())
}

// SSHUser fetches the ssh user defined for the bundle
func (ib *ImageBundleContext) SSHUser() string {
	details := ib.ib.GetDetails()
	return details.GetSshUser()
}

// UseIMDSv2 fetches status of the IMDS V2 of the bundle
func (ib *ImageBundleContext) UseIMDSv2() string {
	details := ib.ib.GetDetails()
	return fmt.Sprintf("%t", details.GetUseIMDSv2())
}

// RegionOverrides fetches region overrides for the bundle
func (ib *ImageBundleContext) RegionOverrides() string {
	regions := ""
	details := ib.ib.GetDetails()
	regionsMap := details.GetRegions()
	for k, v := range regionsMap {
		regions = fmt.Sprintf("%s%s : %s\n", regions, k, v.GetYbImage())
	}
	if len(regions) == 0 {
		return "-"
	}
	regions = regions[0 : len(regions)-1]

	return regions
}

// ImageBundleType fetches type of the bundle
func (ib *ImageBundleContext) ImageBundleType() string {
	metadata := ib.ib.GetMetadata()
	return metadata.GetType()
}

// Version fetches version of the bundle
func (ib *ImageBundleContext) Version() string {
	metadata := ib.ib.GetMetadata()
	return metadata.GetVersion()
}

// MarshalJSON function
func (ib *ImageBundleContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(ib.ib)
}
