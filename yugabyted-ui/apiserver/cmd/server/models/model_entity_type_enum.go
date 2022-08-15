package models
// EntityTypeEnum : The Entity type
type EntityTypeEnum string

// List of EntityTypeEnum
const (
	ENTITYTYPEENUM_BACKUP EntityTypeEnum = "BACKUP"
	ENTITYTYPEENUM_CLUSTER EntityTypeEnum = "CLUSTER"
	ENTITYTYPEENUM_CLUSTER_ALLOW_LIST EntityTypeEnum = "CLUSTER_ALLOW_LIST"
	ENTITYTYPEENUM_PROJECT EntityTypeEnum = "PROJECT"
)
