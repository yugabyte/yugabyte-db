package models

// EntityMetadata - Common metadata for entities
type EntityMetadata struct {

    // Timestamp when the entity was created (UTC)
    CreatedOn *string `json:"created_on"`

    // Timestamp when the entity was last updated (UTC)
    UpdatedOn *string `json:"updated_on"`
}
