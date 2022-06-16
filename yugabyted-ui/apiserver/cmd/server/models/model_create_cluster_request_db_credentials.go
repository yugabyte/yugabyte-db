package models

// CreateClusterRequestDbCredentials - DB admin user credentials for YSQL/YCQL
type CreateClusterRequestDbCredentials struct {

	Ycql DbCredentials `json:"ycql"`

	Ysql DbCredentials `json:"ysql"`
}
