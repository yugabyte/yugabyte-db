/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetypes

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

	// Volume Details
	defaultVolume = "table {{.VolumeType}}\t{{.VolumeSize}}\t{{.MountPath}}"

	volumeTypeHeader = "Volume Type"
	volumeSizeHeader = "Volume Size in GB"
	mountPathHeader  = "Mount Paths"
)

// VolumeContext for volume outputs
type VolumeContext struct {
	formatter.HeaderContext
	formatter.Context
	v ybaclient.VolumeDetails
}

// NewVolumeFormat for formatting output
func NewVolumeFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultVolume
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetVolume initializes the context with the volume data
func (v *VolumeContext) SetVolume(volume ybaclient.VolumeDetails) {
	v.v = volume
}

type volumeContext struct {
	Volume *VolumeContext
}

// Write populates the output table to be displayed in the command line
func (v *VolumeContext) Write(index int) error {
	var err error
	rc := &volumeContext{
		Volume: &VolumeContext{},
	}
	rc.Volume.v = v.v

	// Section 1
	tmpl, err := v.startSubsection(defaultVolume)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	v.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Volume %d: Details", index+1), formatter.BlueColor)))
	v.Output.Write([]byte("\n"))
	if err := v.ContextFormat(tmpl, rc.Volume); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	v.PostFormat(tmpl, NewVolumeContext())

	return nil
}

func (v *VolumeContext) startSubsection(format string) (*template.Template, error) {
	v.Buffer = bytes.NewBufferString("")
	v.ContextHeader = ""
	v.Format = formatter.Format(format)
	v.PreFormat()

	return v.ParseFormat()
}

// NewVolumeContext creates a new context for rendering volumes
func NewVolumeContext() *VolumeContext {
	volumeCtx := VolumeContext{}
	volumeCtx.Header = formatter.SubHeaderContext{

		"VolumeType": volumeTypeHeader,
		"VolumeSize": volumeSizeHeader,
		"MountPath":  mountPathHeader,
	}
	return &volumeCtx
}

// VolumeType fetches InstanceTypes Name
func (v *VolumeContext) VolumeType() string {

	return v.v.GetVolumeType()

}

// VolumeSize fetches InstanceTypes Name
func (v *VolumeContext) VolumeSize() string {

	return fmt.Sprintf("%d", v.v.GetVolumeSizeGB())

}

// MountPath fetches InstanceTypes Name
func (v *VolumeContext) MountPath() string {

	return v.v.GetMountPath()

}

// MarshalJSON function
func (v *VolumeContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(v.v)
}
