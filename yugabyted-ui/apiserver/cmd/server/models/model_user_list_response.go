package models

type UserListResponse struct {

	Data []UserData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
