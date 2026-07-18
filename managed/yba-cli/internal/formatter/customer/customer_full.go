/*
 * Copyright (c) YugabyteDB, Inc.
 */

package customer

import (
	"bytes"
	"encoding/json"
	"os"
	"text/template"

	"github.com/spf13/viper"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/smtp"
)

// FullCustomerContext to render Customer Listing output
type FullCustomerContext struct {
	formatter.HeaderContext
	formatter.Context
	c ybaclient.CustomerDetailsData
}

// SetFullCustomer initializes the context with the customer data
func (fc *FullCustomerContext) SetFullCustomer(
	customer ybaclient.CustomerDetailsData,
) {
	fc.c = customer
}

// NewFullCustomerFormat for formatting output
func NewFullCustomerFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultCustomerListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullCustomerContext struct {
	Customer *Context
}

// Write populates the output table to be displayed in the command line
func (fc *FullCustomerContext) Write() error {
	var err error
	fcc := &fullCustomerContext{
		Customer: &Context{},
	}
	fcc.Customer.c = fc.c

	// Section 1
	tmpl, err := fc.startSubsection(defaultCustomerListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fc.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fc.Output.Write([]byte("\n"))
	if err := fc.ContextFormat(tmpl, fcc.Customer); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fc.PostFormat(tmpl, NewCustomerContext())
	fc.Output.Write([]byte("\n"))

	// Section 2: Customer Listing subSection 1
	tmpl, err = fc.startSubsection(customer1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fc.subSection("Customer Details")
	if err := fc.ContextFormat(tmpl, fcc.Customer); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fc.PostFormat(tmpl, NewCustomerContext())
	fc.Output.Write([]byte("\n"))
	tmpl, err = fc.startSubsection(customer2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fc.ContextFormat(tmpl, fcc.Customer); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fc.PostFormat(tmpl, NewCustomerContext())
	fc.Output.Write([]byte("\n"))

	tmpl, err = fc.startSubsection(alertingData1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fc.subSection("Alerting Data")
	if err := fc.ContextFormat(tmpl, fcc.Customer); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fc.PostFormat(tmpl, NewCustomerContext())
	fc.Output.Write([]byte("\n"))
	tmpl, err = fc.startSubsection(alertingData2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fc.ContextFormat(tmpl, fcc.Customer); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fc.PostFormat(tmpl, NewCustomerContext())
	fc.Output.Write([]byte("\n"))

	if fc.c.SmtpData != nil {
		fc.subSection("SMTP Data")
		smtpCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  smtp.NewSMTPFormat(viper.GetString("output")),
		}
		smtp.Write(smtpCtx, []ybaclient.SmtpData{fc.c.GetSmtpData()})
	}
	return nil
}

func (fc *FullCustomerContext) startSubsection(
	format string,
) (*template.Template, error) {
	fc.Buffer = bytes.NewBufferString("")
	fc.ContextHeader = ""
	fc.Format = formatter.Format(format)
	fc.PreFormat()

	return fc.ParseFormat()
}

func (fc *FullCustomerContext) subSection(name string) {
	fc.Output.Write([]byte("\n"))
	fc.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fc.Output.Write([]byte("\n"))
}

// NewFullCustomerContext creates c new context for rendering customer
func NewFullCustomerContext() *FullCustomerContext {
	customerCtx := FullCustomerContext{}
	customerCtx.Header = formatter.SubHeaderContext{}
	return &customerCtx
}

// MarshalJSON function
func (fc *FullCustomerContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fc.c)
}
