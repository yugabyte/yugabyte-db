/*
 * Copyright (c) YugabyteDB, Inc.
 */

package table

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

	// PlacementBlock Details
	defaultPlacementBlock = "table {{.Cloud}}\t{{.Region}}\t{{.Zone}}" +
		"\t{{.LeaderPreference}}\t{{.MinNumReplicas}}"

	cloudHeader            = "Cloud"
	regionHeader           = "Region"
	zoneHeader             = "Zone"
	leaderPreferenceHeader = "Leader Preference"
	minNumReplicasHeader   = "Minimum Number of Replicas"
)

// PlacementBlockContext for placementBlock outputs
type PlacementBlockContext struct {
	formatter.HeaderContext
	formatter.Context
	p ybaclient.PlacementBlock
}

// NewPlacementBlockFormat for formatting output
func NewPlacementBlockFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultPlacementBlock
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetPlacementBlock initializes the context with the placementBlock data
func (p *PlacementBlockContext) SetPlacementBlock(placementBlock ybaclient.PlacementBlock) {
	p.p = placementBlock
}

type placementBlockContext struct {
	PlacementBlock *PlacementBlockContext
}

// Write populates the output table to be displayed in the command line
func (p *PlacementBlockContext) Write(index int) error {
	var err error
	pc := &placementBlockContext{
		PlacementBlock: &PlacementBlockContext{},
	}
	pc.PlacementBlock.p = p.p

	// Section 1
	tmpl, err := p.startSubsection(defaultPlacementBlock)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	p.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Placement Block %d: Details", index+1), formatter.BlueColor)))
	p.Output.Write([]byte("\n"))
	if err := p.ContextFormat(tmpl, pc.PlacementBlock); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	p.PostFormat(tmpl, NewPlacementBlockContext())

	return nil
}

func (p *PlacementBlockContext) startSubsection(format string) (*template.Template, error) {
	p.Buffer = bytes.NewBufferString("")
	p.ContextHeader = ""
	p.Format = formatter.Format(format)
	p.PreFormat()

	return p.ParseFormat()
}

func (p *PlacementBlockContext) subSection(name string) {
	p.Output.Write([]byte("\n"))
	p.Output.Write([]byte(formatter.Colorize(name, formatter.BlueColor)))
	p.Output.Write([]byte("\n"))
}

// NewPlacementBlockContext creates a new context for rendering placementBlocks
func NewPlacementBlockContext() *PlacementBlockContext {
	placementBlockCtx := PlacementBlockContext{}
	placementBlockCtx.Header = formatter.SubHeaderContext{
		"Cloud":            cloudHeader,
		"Region":           regionHeader,
		"Zone":             zoneHeader,
		"LeaderPreference": leaderPreferenceHeader,
		"MinNumReplicas":   minNumReplicasHeader,
	}
	return &placementBlockCtx
}

// Cloud function
func (p *PlacementBlockContext) Cloud() string {
	return p.p.GetCloud()
}

// Region function
func (p *PlacementBlockContext) Region() string {
	return p.p.GetRegion()
}

// Zone function
func (p *PlacementBlockContext) Zone() string {
	return p.p.GetZone()
}

// LeaderPreference function
func (p *PlacementBlockContext) LeaderPreference() string {
	return fmt.Sprintf("%d", p.p.GetLeaderPreference())
}

// MinNumReplicas function
func (p *PlacementBlockContext) MinNumReplicas() string {
	return fmt.Sprintf("%d", p.p.GetMinNumReplicas())
}

// MarshalJSON function
func (p *PlacementBlockContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(p.p)
}
