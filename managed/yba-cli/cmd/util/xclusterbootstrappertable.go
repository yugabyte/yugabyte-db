/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

// XClusterNeedBootstrapReason for table
type XClusterNeedBootstrapReason string

const (
	// TableMissingOnTarget reason for table
	TableMissingOnTarget XClusterNeedBootstrapReason = "TABLE_MISSING_ON_TARGET"
	// TableHasData reason for table
	TableHasData XClusterNeedBootstrapReason = "TABLE_HAS_DATA"
	// BidirectionalReplication reason for table
	BidirectionalReplication XClusterNeedBootstrapReason = "BIDIRECTIONAL_REPLICATION"
)

// XClusterConfigNeedBootstrapResponse for table
type XClusterConfigNeedBootstrapResponse struct {
	TableUUID     string                                       `json:"tableUUID"`
	BootstrapInfo *XClusterConfigNeedBootstrapPerTableResponse `json:"bootstrapInfo"`
}

// XClusterConfigNeedBootstrapPerTableResponse for table
type XClusterConfigNeedBootstrapPerTableResponse struct {
	IsBootstrapRequired bool                          `json:"bootstrapRequired"`
	Description         string                        `json:"description"`
	Reasons             []XClusterNeedBootstrapReason `json:"reasons"`
}
