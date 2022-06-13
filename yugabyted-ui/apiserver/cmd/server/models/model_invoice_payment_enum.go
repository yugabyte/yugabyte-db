package models
// InvoicePaymentEnum : Invoice payment enum
type InvoicePaymentEnum string

// List of InvoicePaymentEnum
const (
	INVOICEPAYMENTENUM_PROCESSING InvoicePaymentEnum = "PROCESSING"
	INVOICEPAYMENTENUM_PAID InvoicePaymentEnum = "PAID"
	INVOICEPAYMENTENUM_NA InvoicePaymentEnum = "NA"
	INVOICEPAYMENTENUM_UNPAID InvoicePaymentEnum = "UNPAID"
	INVOICEPAYMENTENUM_REFUNDED InvoicePaymentEnum = "REFUNDED"
	INVOICEPAYMENTENUM_FAILED_PAYMENT InvoicePaymentEnum = "FAILED PAYMENT"
)
