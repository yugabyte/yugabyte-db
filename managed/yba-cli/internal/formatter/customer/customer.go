/*
 * Copyright (c) YugaByte, Inc.
 */

package customer

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultCustomerListing = "table {{.Name}}\t{{.UUID}}\t{{.CustomerID}}\t{{.Code}}\t{{.CreationTime}}"

	customer1 = "table {{.CallhomeLevel}}"

	customer2 = "table {{.UniverseUUIDs}}"

	alertingData1 = "table {{.AlertingEmail}}\t{{.CheckInterval}}\t{{.ActiveAlertNotificationInterval}}"

	alertingData2 = "table {{.StatusUpdateInterval}}\t{{.ReportOnlyErrors}}\t{{.SendAlertsToYb}}"

	customer4 = "table {{.SMTPData}}"

	customerIDHeader = "Customer ID"

	callhomeLevelHeader = "Callhome Level"

	universeUUIDsHeader = "Associated Universe UUIDs"

	smtpDataHeader = "SMTP Data"

	alertingEmailHeader = "Alert Emails"

	checkIntervalHeader = "Health Check Interval (in minutes)"

	activeAlertNotificationIntervalHeader = "Active Alert Notification Interval (in minutes)"

	statusUpdateIntervalHeader = "Health Check email report interval (in minutes)"

	reportOnlyErrorsHeader = "Only include errors in alert emails"

	sendAlertsToYbHeader = "Send alerts to Yugabyte Team"
)

// Context for customer outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	c ybaclient.CustomerDetailsData
}

// NewCustomerFormat for formatting output
func NewCustomerFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultCustomerListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for c list of Customers
func Write(ctx formatter.Context, customers []ybaclient.CustomerDetailsData) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of customers into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(customers, "", "  ")
		} else {
			output, err = json.Marshal(customers)
		}

		if err != nil {
			logrus.Errorf("Error marshaling customers to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, customer := range customers {
			err := format(&Context{c: customer})
			if err != nil {
				logrus.Debugf("Error rendering customer: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewCustomerContext(), render)
}

// NewCustomerContext creates c new context for rendering customer
func NewCustomerContext() *Context {
	customerCtx := Context{}
	customerCtx.Header = formatter.SubHeaderContext{
		"Name":                            formatter.NameHeader,
		"UUID":                            formatter.UUIDHeader,
		"CustomerID":                      customerIDHeader,
		"Code":                            formatter.CodeHeader,
		"CreationTime":                    formatter.CreateTimeHeader,
		"CallhomeLevel":                   callhomeLevelHeader,
		"UniverseUUIDs":                   universeUUIDsHeader,
		"AlertingEmail":                   alertingEmailHeader,
		"CheckInterval":                   checkIntervalHeader,
		"ActiveAlertNotificationInterval": activeAlertNotificationIntervalHeader,
		"StatusUpdateInterval":            statusUpdateIntervalHeader,
		"ReportOnlyErrors":                reportOnlyErrorsHeader,
		"SendAlertsToYb":                  sendAlertsToYbHeader,
		"SMTPData":                        smtpDataHeader,
	}
	return &customerCtx
}

// UUID fetches Customer UUID
func (c *Context) UUID() string {
	return c.c.GetUuid()
}

// Name fetches Customer Name
func (c *Context) Name() string {
	return c.c.GetName()
}

// CustomerID fetches Customer ID
func (c *Context) CustomerID() string {
	return fmt.Sprintf("%d", c.c.GetCustomerId())
}

// Code fetches Customer Code
func (c *Context) Code() string {
	return c.c.GetCode()
}

// CreationTime fetches Customer CreationTime
func (c *Context) CreationTime() string {
	return util.PrintTime(c.c.GetCreationDate())
}

// CallhomeLevel fetches Customer CallhomeLevel
func (c *Context) CallhomeLevel() string {
	return c.c.GetCallhomeLevel()
}

// UniverseUUIDs fetches Customer UniverseUUIDs
func (c *Context) UniverseUUIDs() string {
	universeUUIDs := "-"
	for i, v := range c.c.GetUniverseUUIDs() {
		if i == 0 {
			universeUUIDs = v
		} else {
			universeUUIDs = fmt.Sprintf("%s, %s", universeUUIDs, v)
		}
	}
	return universeUUIDs
}

// AlertingEmail fetches Customer AlertingEmail
func (c *Context) AlertingEmail() string {
	data := c.c.GetAlertingData()
	return data.GetAlertingEmail()
}

// CheckInterval fetches Customer CheckInterval
func (c *Context) CheckInterval() string {
	data := c.c.GetAlertingData()
	return fmt.Sprintf("%0.2f", util.ConvertMsToUnit(data.GetCheckIntervalMs(), "MINUTES"))
}

// ActiveAlertNotificationInterval fetches Customer ActiveAlertNotificationInterval
func (c *Context) ActiveAlertNotificationInterval() string {
	data := c.c.GetAlertingData()
	return fmt.Sprintf(
		"%0.2f",
		util.ConvertMsToUnit(data.GetActiveAlertNotificationIntervalMs(), "MINUTES"),
	)
}

// StatusUpdateInterval fetches Customer StatusUpdateInterval
func (c *Context) StatusUpdateInterval() string {
	data := c.c.GetAlertingData()
	return fmt.Sprintf("%0.2f", util.ConvertMsToUnit(data.GetStatusUpdateIntervalMs(), "MINUTES"))
}

// ReportOnlyErrors fetches Customer ReportOnlyErrors
func (c *Context) ReportOnlyErrors() string {
	data := c.c.GetAlertingData()
	return fmt.Sprintf("%t", data.GetReportOnlyErrors())
}

// SendAlertsToYb fetches Customer SendAlertsToYb
func (c *Context) SendAlertsToYb() string {
	data := c.c.GetAlertingData()
	return fmt.Sprintf("%t", data.GetSendAlertsToYb())
}

// SMTPData fetches Customer SMTPData
func (c *Context) SMTPData() string {
	data, err := json.MarshalIndent(c.c.GetSmtpData(), "", "  ")
	if err != nil {
		return fmt.Sprintf("Error marshaling SMTP data: %v", err)
	}
	return string(data)
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.c)
}
