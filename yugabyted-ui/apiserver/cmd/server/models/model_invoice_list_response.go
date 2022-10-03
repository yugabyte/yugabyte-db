package models

type InvoiceListResponse struct {

	Data []InvoiceData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
