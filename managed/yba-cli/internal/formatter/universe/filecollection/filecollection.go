/*
 * Copyright (c) YugabyteDB, Inc.
 */

package filecollection

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultSummaryListing = "table {{.CollectionUUID}}\t{{.AllSucceeded}}\t{{.TotalNodes}}" +
		"\t{{.SuccessfulNodes}}\t{{.FailedNodes}}\t{{.TotalFilesCollected}}" +
		"\t{{.TotalFilesFailed}}\t{{.TotalFilesSkipped}}\t{{.TotalBytesCollected}}" +
		"\t{{.ExecutionTimeMs}}"

	collectionUUIDHeader      = "Collection UUID"
	allSucceededHeader        = "All Succeeded"
	totalNodesHeader          = "Total Nodes"
	successfulNodesHeader     = "Successful Nodes"
	failedNodesHeader         = "Failed Nodes"
	totalFilesCollectedHeader = "Files Collected"
	totalFilesFailedHeader    = "Files Failed"
	totalFilesSkippedHeader   = "Files Skipped"
	totalBytesCollectedHeader = "Bytes Collected"
	executionTimeMsHeader     = "Execution Time (ms)"

	defaultCleanupListing = "table {{.CleanupCollectionUUID}}\t{{.NodesCleaned}}\t{{.Message}}"
	cleanupUUIDHeader     = "Collection UUID"
	nodesCleanedHeader    = "Nodes Cleaned"
	cleanupMessageHeader  = "Message"
)

// SummaryContext for file collection summary output
type SummaryContext struct {
	formatter.HeaderContext
	formatter.Context
	s ybav2client.FileCollectionSummary
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

// WriteSummary renders the context for a CollectFilesResponse
func WriteSummary(ctx formatter.Context, response *ybav2client.CollectFilesResponse) error {
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		var output []byte
		var err error
		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(response, "", "  ")
		} else {
			output, err = json.Marshal(response)
		}
		if err != nil {
			logrus.Errorf("Error marshaling file collection response to json: %v\n", err)
			return err
		}
		_, err = ctx.Output.Write(output)
		return err
	}

	render := func(format func(subContext formatter.SubContext) error) error {
		return format(&SummaryContext{s: response.GetSummary()})
	}
	return ctx.Write(NewSummaryContext(), render)
}

// NewSummaryContext creates a new context for rendering file collection summary
func NewSummaryContext() *SummaryContext {
	ctx := SummaryContext{}
	ctx.Header = formatter.SubHeaderContext{
		"CollectionUUID":      collectionUUIDHeader,
		"AllSucceeded":        allSucceededHeader,
		"TotalNodes":          totalNodesHeader,
		"SuccessfulNodes":     successfulNodesHeader,
		"FailedNodes":         failedNodesHeader,
		"TotalFilesCollected": totalFilesCollectedHeader,
		"TotalFilesFailed":    totalFilesFailedHeader,
		"TotalFilesSkipped":   totalFilesSkippedHeader,
		"TotalBytesCollected": totalBytesCollectedHeader,
		"ExecutionTimeMs":     executionTimeMsHeader,
	}
	return &ctx
}

// CollectionUUID fetches the collection UUID.
func (c *SummaryContext) CollectionUUID() string {
	uuid := c.s.GetCollectionUuid()
	if uuid == "" {
		return "N/A (no files collected)"
	}
	return uuid
}

// AllSucceeded fetches whether all nodes succeeded.
func (c *SummaryContext) AllSucceeded() string {
	return fmt.Sprintf("%t", c.s.GetAllSucceeded())
}

// TotalNodes fetches the total number of nodes
func (c *SummaryContext) TotalNodes() string {
	return fmt.Sprintf("%d", c.s.GetTotalNodes())
}

// SuccessfulNodes fetches successful node count
func (c *SummaryContext) SuccessfulNodes() string {
	return fmt.Sprintf("%d", c.s.GetSuccessfulNodes())
}

// FailedNodes fetches failed node count
func (c *SummaryContext) FailedNodes() string {
	return fmt.Sprintf("%d", c.s.GetFailedNodes())
}

// TotalFilesCollected fetches total files collected
func (c *SummaryContext) TotalFilesCollected() string {
	return fmt.Sprintf("%d", c.s.GetTotalFilesCollected())
}

// TotalFilesFailed fetches total files that failed collection
func (c *SummaryContext) TotalFilesFailed() string {
	return fmt.Sprintf("%d", c.s.GetTotalFilesFailed())
}

// TotalFilesSkipped fetches total files skipped during collection
func (c *SummaryContext) TotalFilesSkipped() string {
	return fmt.Sprintf("%d", c.s.GetTotalFilesSkipped())
}

// TotalBytesCollected fetches total bytes collected
func (c *SummaryContext) TotalBytesCollected() string {
	bytes := c.s.GetTotalBytesCollected()
	if bytes > 1024*1024 {
		return fmt.Sprintf("%.2f MB", float64(bytes)/(1024*1024))
	} else if bytes > 1024 {
		return fmt.Sprintf("%.2f KB", float64(bytes)/1024)
	}
	return fmt.Sprintf("%d B", bytes)
}

// ExecutionTimeMs fetches execution time
func (c *SummaryContext) ExecutionTimeMs() string {
	return fmt.Sprintf("%d", c.s.GetTotalExecutionTimeMs())
}

// MarshalJSON function
func (c *SummaryContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.s)
}

// CleanupContext for delete file collection output
type CleanupContext struct {
	formatter.HeaderContext
	formatter.Context
	c ybav2client.CleanupCollectionInfo
}

// NewCleanupFormat for formatting output
func NewCleanupFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		return formatter.Format(defaultCleanupListing)
	default:
		return formatter.Format(source)
	}
}

// WriteCleanup renders the context for a CleanupCollectionInfo
func WriteCleanup(ctx formatter.Context, info *ybav2client.CleanupCollectionInfo) error {
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		var output []byte
		var err error
		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(info, "", "  ")
		} else {
			output, err = json.Marshal(info)
		}
		if err != nil {
			logrus.Errorf("Error marshaling cleanup info to json: %v\n", err)
			return err
		}
		_, err = ctx.Output.Write(output)
		return err
	}

	render := func(format func(subContext formatter.SubContext) error) error {
		return format(&CleanupContext{c: *info})
	}
	return ctx.Write(NewCleanupContext(), render)
}

// NewCleanupContext creates a new context for rendering cleanup info
func NewCleanupContext() *CleanupContext {
	ctx := CleanupContext{}
	ctx.Header = formatter.SubHeaderContext{
		"CleanupCollectionUUID": cleanupUUIDHeader,
		"NodesCleaned":          nodesCleanedHeader,
		"Message":               cleanupMessageHeader,
	}
	return &ctx
}

// CleanupCollectionUUID fetches the collection UUID
func (c *CleanupContext) CleanupCollectionUUID() string {
	return c.c.GetCollectionUuid()
}

// NodesCleaned fetches the number of nodes cleaned
func (c *CleanupContext) NodesCleaned() string {
	return fmt.Sprintf("%d", c.c.GetNodesCleaned())
}

// Message fetches the cleanup message
func (c *CleanupContext) Message() string {
	return c.c.GetMessage()
}

// MarshalJSON function
func (c *CleanupContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.c)
}
