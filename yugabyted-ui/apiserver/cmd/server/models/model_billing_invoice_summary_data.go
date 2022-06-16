package models

// BillingInvoiceSummaryData - Billing invoice summary data
type BillingInvoiceSummaryData struct {

	// Invoice status
	Status string `json:"status"`

	// Invoice unique id
	InvoiceId string `json:"invoice_id"`

	// Invoice serial number
	InvoiceSerialNo *string `json:"invoice_serial_no"`

	// Invoice PDF downloadble link
	InvoicePdfLink *string `json:"invoice_pdf_link"`

	// Total invoice amount
	TotalAmount *float64 `json:"total_amount"`

	// Billing option i.e PayG or Subscription
	BillingOption string `json:"billing_option"`

	// Billing cycle period
	BillingPeriod string `json:"billing_period"`

	// Previous invoiced amount
	PreviousInvoiceAmount *float64 `json:"previous_invoice_amount"`

	// Previous invoice generated date
	LastBilledOn *string `json:"last_billed_on"`

	PaymentStatus InvoicePaymentEnum `json:"payment_status"`

	PaymentDate *string `json:"payment_date"`

	PaymentMethodDetail *string `json:"payment_method_detail"`

	PaymentMethodBrand *string `json:"payment_method_brand"`

	ThisTimeLastMonthAmount *float64 `json:"this_time_last_month_amount"`

	AmountPercentageChangeFromLastMonth *float64 `json:"amount_percentage_change_from_last_month"`
}
