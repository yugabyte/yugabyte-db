/*
* Copyright (c) YugabyteDB, Inc.
 */

package schedule

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullScheduleContext to render schedule Details output
type FullScheduleContext struct {
	formatter.HeaderContext
	formatter.Context
	s util.Schedule
}

// SetFullSchedule initializes the context with the schedule data
func (fs *FullScheduleContext) SetFullSchedule(schedule util.Schedule) {
	fs.s = schedule
}

// NewFullScheduleFormat for formatting output
func NewFullScheduleFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultScheduleListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullScheduleContext struct {
	Schedule *Context
}

// Write populates the output table to be displayed in the command line
func (fs *FullScheduleContext) Write() error {
	var err error
	fsc := &fullScheduleContext{
		Schedule: &Context{},
	}
	fsc.Schedule.s = fs.s

	// Section 1
	tmpl, err := fs.startSubsection(defaultScheduleListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fs.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fs.Output.Write([]byte("\n"))
	if err := fs.ContextFormat(tmpl, fsc.Schedule); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fs.PostFormat(tmpl, NewScheduleContext())
	fs.Output.Write([]byte("\n"))

	// Schedule information
	tmpl, err = fs.startSubsection(scheduleListing1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fs.ContextFormat(tmpl, fsc.Schedule); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fs.PostFormat(tmpl, NewScheduleContext())
	fs.Output.Write([]byte("\n"))

	tmpl, err = fs.startSubsection(scheduleListing2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fs.ContextFormat(tmpl, fsc.Schedule); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fs.PostFormat(tmpl, NewScheduleContext())
	fs.Output.Write([]byte("\n"))
	return nil
}

func (fs *FullScheduleContext) startSubsection(format string) (*template.Template, error) {
	fs.Buffer = bytes.NewBufferString("")
	fs.ContextHeader = ""
	fs.Format = formatter.Format(format)
	fs.PreFormat()

	return fs.ParseFormat()
}

func (fs *FullScheduleContext) subSection(name string) {
	fs.Output.Write([]byte("\n"))
	fs.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fs.Output.Write([]byte("\n"))
}

// NewFullScheduleContext creates a new context for rendering schedule
func NewFullScheduleContext() *FullScheduleContext {
	scheduleCtx := FullScheduleContext{}
	scheduleCtx.Header = formatter.SubHeaderContext{}
	return &scheduleCtx
}

// MarshalJSON function
func (fs *FullScheduleContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fs.s)
}
