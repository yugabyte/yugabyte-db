package models

// UserSpec - User specification
type UserSpec struct {

	// The email of the user
	Email string `json:"email"`

	// The first name associated with the user
	FirstName *string `json:"first_name"`

	// The last name associated with the user
	LastName *string `json:"last_name"`

	// Company name associated with the user
	CompanyName *string `json:"company_name"`

	// Two letter country code as defined in ISO 3166
	CountryCode *string `json:"country_code"`

	// Marketing consent of the user
	MarketingConsent *bool `json:"marketing_consent"`
}
