/*
 * Copyright (c) YugaByte, Inc.
 */

package eit

import (
	"encoding/json"
	"fmt"
	"time"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultEITListing = "table {{.Name}}\t{{.UUID}}\t{{.CreationDate}}" +
		"\t{{.ExpirationDate}}\t{{.CertType}}\t{{.InUse}}"

	inUseHeader          = "In Use"
	certTypeHeader       = "Certificate Type"
	creationDateHeader   = "Creation Date"
	expirationDateHeader = "Expiration Date"
	certificateHeader    = "Certificate"
	privateKeyHeader     = "Private Key"
)

// Context for eit config outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	eit ybaclient.CertificateInfoExt
}

// NewEITFormat for formatting output
func NewEITFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultEITListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of EITs
func Write(ctx formatter.Context, eits []ybaclient.CertificateInfoExt) error {
	// Check if the format is JSON or Pretty JSON
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		// Marshal the slice of eits into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(eits, "", "  ")
		} else {
			output, err = json.Marshal(eits)
		}

		if err != nil {
			logrus.Errorf("Error marshaling EIT configs to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, eit := range eits {
			err := format(&Context{eit: eit})
			if err != nil {
				logrus.Debugf("Error rendering eit config: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewEITContext(), render)
}

// NewEITContext creates a new context for rendering eit
func NewEITContext() *Context {
	eitCtx := Context{}
	eitCtx.Header = formatter.SubHeaderContext{
		"Name":           formatter.NameHeader,
		"UUID":           formatter.UUIDHeader,
		"CreationDate":   creationDateHeader,
		"ExpirationDate": expirationDateHeader,
		"CertType":       certTypeHeader,
		"InUse":          inUseHeader,
		"Certificate":    certificateHeader,
		"PrivateKey":     privateKeyHeader,
	}
	return &eitCtx
}

// UUID fetches EIT UUID
func (c *Context) UUID() string {
	return c.eit.GetUuid()
}

// Name fetches EIT Name
func (c *Context) Name() string {
	return c.eit.GetLabel()
}

// CreationDate fetches EIT Creation Date
func (c *Context) CreationDate() string {
	creationTime := c.eit.GetStartDateIso()
	if creationTime.IsZero() {
		return ""
	}
	return creationTime.Format(time.RFC1123Z)
}

// ExpirationDate fetches EIT Expiration Date
func (c *Context) ExpirationDate() string {
	expirationTime := c.eit.GetExpiryDateIso()
	if expirationTime.IsZero() {
		return ""
	}
	return expirationTime.Format(time.RFC1123Z)

}

// CertType fetches EIT Certificate Type
func (c *Context) CertType() string {
	return c.eit.GetCertType()
}

// InUse fetches EIT In Use
func (c *Context) InUse() string {
	return fmt.Sprintf("%t", c.eit.GetInUse())
}

// Certificate fetches EIT Certificate
func (c *Context) Certificate() string {
	return c.eit.GetCertificate()
}

// PrivateKey fetches EIT Private Key
func (c *Context) PrivateKey() string {
	return c.eit.GetPrivateKey()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.eit)
}
