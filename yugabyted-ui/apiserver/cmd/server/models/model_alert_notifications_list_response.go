package models

type AlertNotificationsListResponse struct {

	Data []AlertNotificationData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
