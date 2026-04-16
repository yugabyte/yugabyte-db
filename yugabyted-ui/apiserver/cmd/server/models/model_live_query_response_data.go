package models

// LiveQueryResponseData - Live Query Response Data
type LiveQueryResponseData struct {

    Ysql LiveQueryResponseYsqlData `json:"ysql"`

    Ycql LiveQueryResponseYcqlData `json:"ycql"`
}
