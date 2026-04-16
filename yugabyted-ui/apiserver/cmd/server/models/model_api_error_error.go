package models

type ApiErrorError struct {

    // Error message
    Detail string `json:"detail"`

    // Error code
    Status int32 `json:"status"`
}
