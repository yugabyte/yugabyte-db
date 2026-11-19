/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// UpdateReleaseRequest is the body of PUT /ybdb_release/{uuid}. The server deletes every
// artifact missing from Artifacts and stores every artifact field it receives, so an empty
// string overwrites the stored value. ybaclient.UpdateRelease sends "" for each unset artifact
// field, so it can't be used to keep artifacts unchanged.
type UpdateReleaseRequest struct {
	Artifacts    []UpdateReleaseArtifact `json:"artifacts"`
	ReleaseDate  int64                   `json:"release_date"`
	ReleaseNotes string                  `json:"release_notes"`
	ReleaseTag   string                  `json:"release_tag"`
	State        string                  `json:"state"`
}

// UpdateReleaseArtifact names an artifact to keep. The server matches it to a stored artifact by
// platform and architecture and leaves that artifact unchanged.
type UpdateReleaseArtifact struct {
	Platform     string `json:"platform"`
	Architecture string `json:"architecture,omitempty"`
}

// KeepReleaseArtifacts lists every artifact of a release so an update leaves them in place.
func KeepReleaseArtifacts(artifacts []ybaclient.Artifact) []UpdateReleaseArtifact {
	keep := make([]UpdateReleaseArtifact, 0, len(artifacts))
	for _, a := range artifacts {
		keep = append(keep, UpdateReleaseArtifact{
			Platform:     a.GetPlatform(),
			Architecture: a.GetArchitecture(),
		})
	}
	return keep
}
