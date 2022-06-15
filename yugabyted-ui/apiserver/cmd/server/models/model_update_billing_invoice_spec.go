package models

// UpdateBillingInvoiceSpec - Update invoice status of a billing account
type UpdateBillingInvoiceSpec struct {

	InvoiceStatus InvoiceStatusEnum `json:"invoice_status"`

	PaymentStatus InvoicePaymentEnum `json:"payment_status"`
}
