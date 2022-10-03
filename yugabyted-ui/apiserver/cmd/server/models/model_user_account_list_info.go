package models

type UserAccountListInfo struct {

	// List of admin accounts associated with the user
	OwnedAccounts *[]AccountData `json:"owned_accounts"`

	// List of admin accounts associated with the user
	AdminAccounts *[]AccountData `json:"admin_accounts"`

	// List of all accounts associated with the user
	LinkedAccounts *[]AccountData `json:"linked_accounts"`

	// List of all accounts associated with the user
	Accounts *[]AccountData `json:"accounts"`
}
