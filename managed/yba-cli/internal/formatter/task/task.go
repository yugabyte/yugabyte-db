/*
 * Copyright (c) YugaByte, Inc.
 */

package task

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultTaskListing = "table {{.Title}}\t{{.UUID}}\t{{.Target}}" +
		"\t{{.Status}}\t{{.CreateTime}}\t{{.CompletionTime}}"

	titleHeader          = "Title"
	targetHeader         = "Target(Target UUID)"
	createTimeHeader     = "Creation Time"
	completionTimeHeader = "Completion Time"
)

// Context for task outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	t ybaclient.CustomerTaskData
}

// NewTaskFormat for formatting output
func NewTaskFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultTaskListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Tasks
func Write(ctx formatter.Context, tasks []ybaclient.CustomerTaskData) error {
	// Check if the format is JSON or Pretty JSON
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
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
		"Title":  titleHeader,
		"UUID":   formatter.UUIDHeader,
		"Target": targetHeader,

		"Status":         formatter.StatusHeader,
		"CreateTime":     createTimeHeader,
		"CompletionTime": completionTimeHeader,
	}
	return &taskCtx
}

// UUID fetches Task UUID
func (c *Context) UUID() string {
	return c.t.GetId()
}

// Title fetches Task title
func (c *Context) Title() string {
	return c.t.GetTitle()
}

// Target fetches Task Target
func (c *Context) Target() string {
	target := fmt.Sprintf("%s(%s)", c.t.GetTarget(), c.t.GetTargetUUID())
	return target
}

// Status fetches the task state
func (c *Context) Status() string {
	return c.t.GetStatus()
}

// CreateTime fetches Task create time
func (c *Context) CreateTime() string {
	return c.t.GetCreateTime().String()
}

// CompletionTime fetches Task create time
func (c *Context) CompletionTime() string {
	return c.t.GetCompletionTime().String()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.t)
}
