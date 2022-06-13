package models

type InviteUserSpec struct {

	// The email associated with the user
	Email string `json:"email"`

	// The first name associated with the user
	FirstName *string `json:"first_name"`

	// The last name associated with the user
	LastName *string `json:"last_name"`

	// The company associated with the user
	CompanyName *string `json:"company_name"`

	// The role id of the user
	RoleId *string `json:"role_id"`

	// Password to associate with newly invited user
	Password *string `json:"password"`

	// Two letter country code as defined in ISO 3166
	CountryCode *string `json:"country_code"`

	// Marketing consent of the user
	MarketingConsent *bool `json:"marketing_consent"`
}
