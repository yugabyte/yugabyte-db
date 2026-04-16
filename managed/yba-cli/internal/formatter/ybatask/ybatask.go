/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ybatask

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultTaskListing = "table {{.TaskUUID}}\t{{.ResourceUUID}}"

	taskUUIDHeader     = "Task UUID"
	resourceUUIDHeader = "Resource UUID"
)

// Context for task outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	t ybaclient.YBPTask
}

// NewTaskFormat for formatting output
func NewTaskFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultTaskListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Tasks
func Write(ctx formatter.Context, tasks [](ybaclient.YBPTask)) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of tasks into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(tasks, "", "  ")
		} else {
			output, err = json.Marshal(tasks)
		}

		if err != nil {
			logrus.Errorf("Error marshaling tasks to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, task := range tasks {
			err := format(&Context{t: task})
			if err != nil {
				logrus.Debugf("Error rendering task: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewTaskContext(), render)
}

// NewTaskContext creates a new context for rendering task
func NewTaskContext() *Context {
	taskCtx := Context{}
	taskCtx.Header = formatter.SubHeaderContext{
		"TaskUUID":     taskUUIDHeader,
		"ResourceUUID": resourceUUIDHeader,
	}
	return &taskCtx
}

// TaskUUID fetches the task UUID
func (c *Context) TaskUUID() string {
	return c.t.GetTaskUUID()
}

// ResourceUUID fetches the resource UUID
func (c *Context) ResourceUUID() string {
	return c.t.GetResourceUUID()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.t)
}
