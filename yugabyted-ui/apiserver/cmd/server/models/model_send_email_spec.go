package models

// SendEmailSpec - Send email spec
type SendEmailSpec struct {

	EmailTemplate EmailTemplateEnum `json:"email_template"`

	// Send email corresponding to user
	UserEmail *string `json:"user_email"`

	// Source email address
	FromAddress *string `json:"from_address"`

	// Destination email address
	ToAddress *string `json:"to_address"`

	// Reply to Address(es)
	ReplyToAddresses *[]string `json:"reply_to_addresses"`

	// CC Address(es)
	CcAddresses *[]string `json:"cc_addresses"`

	// Email template mapping values
	Mappings *map[string]string `json:"mappings"`
}
