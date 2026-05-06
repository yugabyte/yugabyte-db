/*
 * Copyright (c) YugabyteDB, Inc.
 */

package runscript

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultSummaryListing = "table {{.AllSucceeded}}\t{{.TotalNodes}}" +
		"\t{{.SuccessfulNodes}}\t{{.FailedNodes}}\t{{.ExecutionTimeMs}}"

	allSucceededHeader    = "All Succeeded"
	totalNodesHeader      = "Total Nodes"
	successfulNodesHeader = "Successful Nodes"
	failedNodesHeader     = "Failed Nodes"
	executionTimeMsHeader = "Execution Time (ms)"

	defaultResultListing = "table {{.NodeName}}\t{{.NodeAddress}}\t{{.ExitCode}}" +
		"\t{{.Success}}\t{{.ExecutionTimeMs}}"

	nodeNameHeader         = "Node Name"
	nodeAddressHeader      = "Node Address"
	exitCodeHeader         = "Exit Code"
	successHeader          = "Success"
	resultExecTimeMsHeader = "Execution Time (ms)"
)

// SummaryContext for run-script summary output
type SummaryContext struct {
	formatter.HeaderContext
	formatter.Context
	s ybav2client.ExecutionSummary
}

// NewSummaryFormat for formatting output
func NewSummaryFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		return formatter.Format(defaultSummaryListing)
	default:
		return formatter.Format(source)
	}
}

// WriteResponse renders the full RunScriptResponse
func WriteResponse(ctx formatter.Context, response *ybav2client.RunScriptResponse) error {
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		var output []byte
		var err error
		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(response, "", "  ")
		} else {
			output, err = json.Marshal(response)
		}
		if err != nil {
			logrus.Errorf("Error marshaling run-script response to json: %v\n", err)
			return err
		}
		_, err = ctx.Output.Write(output)
		return err
	}

	// Write summary table
	summaryRender := func(format func(subContext formatter.SubContext) error) error {
		return format(&SummaryContext{s: response.GetSummary()})
	}
	if err := ctx.Write(NewSummaryContext(), summaryRender); err != nil {
		return err
	}

	ctx.Output.Write([]byte("\n"))

	// Write per-node results table
	resultCtx := formatter.Context{
		Command: ctx.Command,
		Output:  ctx.Output,
		Format:  formatter.Format(defaultResultListing),
	}
	resultRender := func(format func(subContext formatter.SubContext) error) error {
		for _, result := range response.GetResults() {
			if err := format(&ResultContext{r: result}); err != nil {
				return err
			}
		}
		return nil
	}
	if err := resultCtx.Write(NewResultContext(), resultRender); err != nil {
		return err
	}

	// Print stdout/stderr for each node
	for nodeName, result := range response.GetResults() {
		stdout := result.GetStdout()
		stderr := result.GetStderr()
		errMsg := result.GetErrorMessage()

		if stdout != "" || stderr != "" || errMsg != "" {
			ctx.Output.Write([]byte("\n"))
			ctx.Output.Write([]byte(formatter.Colorize(
				fmt.Sprintf("--- %s ---", nodeName), formatter.GreenColor)))
			ctx.Output.Write([]byte("\n"))
		}
		if stdout != "" {
			ctx.Output.Write([]byte("stdout:\n"))
			ctx.Output.Write([]byte(stdout))
			if stdout[len(stdout)-1] != '\n' {
				ctx.Output.Write([]byte("\n"))
			}
		}
		if stderr != "" {
			ctx.Output.Write([]byte(formatter.Colorize("stderr:\n", formatter.YellowColor)))
			ctx.Output.Write([]byte(stderr))
			if stderr[len(stderr)-1] != '\n' {
				ctx.Output.Write([]byte("\n"))
			}
		}
		if errMsg != "" {
			ctx.Output.Write([]byte(formatter.Colorize("error: "+errMsg+"\n", formatter.RedColor)))
		}
	}

	return nil
}

// NewSummaryContext creates a new context for rendering execution summary
func NewSummaryContext() *SummaryContext {
	ctx := SummaryContext{}
	ctx.Header = formatter.SubHeaderContext{
		"AllSucceeded":    allSucceededHeader,
		"TotalNodes":      totalNodesHeader,
		"SuccessfulNodes": successfulNodesHeader,
		"FailedNodes":     failedNodesHeader,
		"ExecutionTimeMs": executionTimeMsHeader,
	}
	return &ctx
}

// AllSucceeded fetches whether all nodes succeeded
func (c *SummaryContext) AllSucceeded() string {
	if c.s.GetAllSucceeded() {
		return formatter.Colorize("true", formatter.GreenColor)
	}
	return formatter.Colorize("false", formatter.RedColor)
}

// TotalNodes fetches total node count
func (c *SummaryContext) TotalNodes() string {
	return fmt.Sprintf("%d", c.s.GetTotalNodes())
}

// SuccessfulNodes fetches successful node count
func (c *SummaryContext) SuccessfulNodes() string {
	return fmt.Sprintf("%d", c.s.GetSuccessfulNodes())
}

// FailedNodes fetches failed node count
func (c *SummaryContext) FailedNodes() string {
	count := c.s.GetFailedNodes()
	text := fmt.Sprintf("%d", count)
	if count > 0 {
		return formatter.Colorize(text, formatter.RedColor)
	}
	return text
}

// ExecutionTimeMs fetches execution time
func (c *SummaryContext) ExecutionTimeMs() string {
	return fmt.Sprintf("%d", c.s.GetTotalExecutionTimeMs())
}

// MarshalJSON function
func (c *SummaryContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.s)
}

// ResultContext for per-node script result output
type ResultContext struct {
	formatter.HeaderContext
	formatter.Context
	r ybav2client.NodeScriptResult
}

// NewResultContext creates a new context for rendering per-node results
func NewResultContext() *ResultContext {
	ctx := ResultContext{}
	ctx.Header = formatter.SubHeaderContext{
		"NodeName":        nodeNameHeader,
		"NodeAddress":     nodeAddressHeader,
		"ExitCode":        exitCodeHeader,
		"Success":         successHeader,
		"ExecutionTimeMs": resultExecTimeMsHeader,
	}
	return &ctx
}

// NodeName fetches node name
func (c *ResultContext) NodeName() string {
	return c.r.GetNodeName()
}

// NodeAddress fetches node address
func (c *ResultContext) NodeAddress() string {
	return c.r.GetNodeAddress()
}

// ExitCode fetches exit code
func (c *ResultContext) ExitCode() string {
	code := c.r.GetExitCode()
	text := fmt.Sprintf("%d", code)
	if code != 0 {
		return formatter.Colorize(text, formatter.RedColor)
	}
	return formatter.Colorize(text, formatter.GreenColor)
}

// Success fetches success status
func (c *ResultContext) Success() string {
	if c.r.GetSuccess() {
		return formatter.Colorize("true", formatter.GreenColor)
	}
	return formatter.Colorize("false", formatter.RedColor)
}

// ExecutionTimeMs fetches per-node execution time
func (c *ResultContext) ExecutionTimeMs() string {
	return fmt.Sprintf("%d", c.r.GetExecutionTimeMs())
}

// MarshalJSON function
func (c *ResultContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.r)
}
