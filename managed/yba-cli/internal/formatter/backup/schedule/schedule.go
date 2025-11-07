/*
 * Copyright (c) YugabyteDB, Inc.
 */

package schedule

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
	defaultScheduleListing = "table {{.Name}}\t{{.UUID}}" +
		"\t{{.Frequency}}\t{{.CronExpression}}\t{{.State}}"
	scheduleListing1 = "table {{.StorageConfiguration}}\t{{.KMSConfig}}"
	scheduleListing2 = "table {{.TaskType}}\t{{.NextTaskTime}}"

	taskTypeHeader              = "Task Type"
	nextIncrementTaskTime       = "Next Increment Backup Task Time"
	frequencyHeader             = "Frequency"
	cronExpressionHeader        = "Cron Expression"
	nextTaskTimeHeader          = "Next Task Time"
	nextIncrementTaskTimeHeader = "Next Increment Task Time"
)

// StorageConfigs hold storage config for the backup
var StorageConfigs []ybaclient.CustomerConfigUI

// KMSConfigs hold KMS config for the backup
var KMSConfigs []util.KMSConfig

// Context for provider outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	s util.Schedule
}

// NewScheduleFormat for formatting output
func NewScheduleFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultScheduleListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Schedules
func Write(ctx formatter.Context, schedules []util.Schedule) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of schedules into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(schedules, "", "  ")
		} else {
			output, err = json.Marshal(schedules)
		}

		if err != nil {
			logrus.Errorf("Error marshaling schedules to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, schedule := range schedules {
			err := format(&Context{s: schedule})
			if err != nil {
				logrus.Debugf("Error rendering schedule: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewScheduleContext(), render)
}

// NewScheduleContext creates a new context for rendering provider
func NewScheduleContext() *Context {
	scheduleCtx := Context{}
	scheduleCtx.Header = formatter.SubHeaderContext{
		"Name":                  formatter.NameHeader,
		"UUID":                  formatter.UUIDHeader,
		"StorageConfiguration":  formatter.StorageConfigurationHeader,
		"State":                 formatter.StateHeader,
		"Frequency":             frequencyHeader,
		"CronExpression":        cronExpressionHeader,
		"NextTaskTime":          nextTaskTimeHeader,
		"NextIncrementTaskTime": nextIncrementTaskTimeHeader,
		"TaskType":              taskTypeHeader,
		"KMSConfig":             formatter.KMSConfigHeader,
	}
	return &scheduleCtx
}

// UUID fetches Schedule UUID
func (c *Context) UUID() string {
	return c.s.GetScheduleUUID()
}

// Name fetches Schedule Name
func (c *Context) Name() string {
	return c.s.GetScheduleName()
}

// StorageConfiguration fetches Storage Config Name
func (c *Context) StorageConfiguration() string {
	commonBackupInfo := c.s.GetCommonBackupInfo()
	for _, config := range StorageConfigs {
		if strings.Compare(config.GetConfigUUID(), commonBackupInfo.GetStorageConfigUUID()) == 0 {
			return fmt.Sprintf("%s(%s)", config.GetConfigName(), config.GetConfigUUID())
		}
	}
	// get name too
	return commonBackupInfo.GetStorageConfigUUID()
}

// KMSConfig fetches KMS Config
func (c *Context) KMSConfig() string {
	commonBackupInfo := c.s.GetCommonBackupInfo()
	for _, k := range KMSConfigs {
		if len(strings.TrimSpace(k.ConfigUUID)) != 0 &&
			strings.Compare(k.ConfigUUID, commonBackupInfo.GetKmsConfigUUID()) == 0 {
			return fmt.Sprintf("%s(%s)", k.Name, commonBackupInfo.GetKmsConfigUUID())
		}
	}
	return commonBackupInfo.GetKmsConfigUUID()
}

// State fetches Schedule State
func (c *Context) State() string {
	state := c.s.GetStatus()
	if strings.Compare(state, util.ActiveScheduleBackupState) == 0 {
		return formatter.Colorize(state, formatter.GreenColor)
	}
	if strings.Compare(state, util.ErrorScheduleBackupState) == 0 {
		return formatter.Colorize(state, formatter.RedColor)
	}
	return formatter.Colorize(state, formatter.YellowColor)

}

// Frequency fetches Schedule Frequency
func (c *Context) Frequency() string {
	frequency := util.ConvertMsToUnit(c.s.GetFrequency(), c.s.GetFrequencyTimeUnit())
	return fmt.Sprintf("%0.0f %s", frequency, c.s.GetFrequencyTimeUnit())
}

// CronExpression fetches Schedule Cron Expression
func (c *Context) CronExpression() string {
	return c.s.GetCronExpression()
}

// NextTaskTime fetches Schedule Next Task Time
func (c *Context) NextTaskTime() string {
	return util.PrintCustomTime(c.s.GetNextScheduleTaskTime())
}

// NextIncrementTaskTime fetches Schedule Next Increment Task Time
func (c *Context) NextIncrementTaskTime() string {
	return util.PrintCustomTime(c.s.GetNextIncrementScheduleTaskTime())
}

// TaskType fetches Schedule Task Type
func (c *Context) TaskType() string {
	return c.s.GetTaskType()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.s)
}
