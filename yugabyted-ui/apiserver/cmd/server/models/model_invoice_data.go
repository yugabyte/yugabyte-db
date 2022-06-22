package models

// InvoiceData - Invoice data
type InvoiceData struct {

	InvoiceId string `json:"invoice_id"`

	TotalAmount float64 `json:"total_amount"`

	PdfLink string `json:"pdf_link"`

	InvoicedOn string `json:"invoiced_on"`

	StartDate string `json:"start_date"`

	EndDate string `json:"end_date"`

	PaymentStatus string `json:"payment_status"`

	InvoiceNumber string `json:"invoice_number"`

	InvoiceStatus string `json:"invoice_status"`
}
