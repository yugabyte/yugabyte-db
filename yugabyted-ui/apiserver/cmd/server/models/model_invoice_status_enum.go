package models
// InvoiceStatusEnum : Invoice status enum
type InvoiceStatusEnum string

// List of InvoiceStatusEnum
const (
	INVOICESTATUSENUM_DRAFT InvoiceStatusEnum = "DRAFT"
	INVOICESTATUSENUM_RUNNING InvoiceStatusEnum = "RUNNING"
	INVOICESTATUSENUM_INVOICED InvoiceStatusEnum = "INVOICED"
	INVOICESTATUSENUM_PENDING InvoiceStatusEnum = "PENDING"
	INVOICESTATUSENUM_DISPUTE InvoiceStatusEnum = "DISPUTE"
	INVOICESTATUSENUM_FORGIVEN InvoiceStatusEnum = "FORGIVEN"
)
