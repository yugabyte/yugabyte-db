package models

// ActivityData - Activity Data
type ActivityData struct {

    Name string `json:"name"`

    Data map[string]interface{} `json:"data"`
}
