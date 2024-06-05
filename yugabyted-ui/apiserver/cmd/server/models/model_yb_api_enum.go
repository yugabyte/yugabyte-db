package models
// YbApiEnum : Type of DB API (YSQL/YCQL)
type YbApiEnum string

// List of YbApiEnum
const (
    YBAPIENUM_YSQL YbApiEnum = "YSQL"
    YBAPIENUM_YCQL YbApiEnum = "YCQL"
)
