package models

// UnsupportedSqlInfo - Unsupported SQL Info
type UnsupportedSqlInfo struct {

    UnsupportedType string `json:"unsupported_type"`

    Count int32 `json:"count"`

    Objects []UnsupportedSqlObjectData `json:"objects"`

    DocsLink string `json:"docs_link"`
}
