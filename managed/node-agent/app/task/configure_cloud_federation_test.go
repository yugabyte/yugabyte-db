// Copyright (c) YugabyteDB, Inc.

package task

import (
	pb "node-agent/generated/service"
	"testing"
)

func gcsHandler(audience string) *ConfigureCloudFederation {
	return &ConfigureCloudFederation{
		param: &pb.ConfigureCloudFederationInput{
			FlowDirection: pb.ConfigureCloudFederationInput_GCS_ON_AWS,
			Config: &pb.ConfigureCloudFederationInput_GcsOnAws{
				GcsOnAws: &pb.GcsOnAwsConfig{Audience: audience},
			},
		},
	}
}

// The stamp/no-op decision hinges on desiredStateHash being stable for identical inputs and
// sensitive to every input that changes an on-node artifact — otherwise we'd either restart YBC
// on every backup or fail to restart after a real config change.
func TestDesiredStateHash(t *testing.T) {
	mustHash := func(h *ConfigureCloudFederation, ybHome string) string {
		t.Helper()
		s, err := h.desiredStateHash(ybHome)
		if err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		return s
	}

	audA := "//iam.googleapis.com/projects/1/locations/global/workloadIdentityPools/p/providers/a"
	audB := "//iam.googleapis.com/projects/1/locations/global/workloadIdentityPools/p/providers/b"
	gcs1 := mustHash(gcsHandler(audA), "/home/yugabyte")
	gcs1b := mustHash(gcsHandler(audA), "/home/yugabyte")
	if gcs1 != gcs1b {
		t.Errorf("hash not stable for identical inputs: %s vs %s", gcs1, gcs1b)
	}
	if gcs2 := mustHash(gcsHandler(audB), "/home/yugabyte"); gcs1 == gcs2 {
		t.Error("hash must change when the audience changes")
	}
	if gcsHome := mustHash(gcsHandler(audA), "/other/home"); gcs1 == gcsHome {
		t.Error("hash must change when ybHome changes")
	}

	// Missing required inputs must error, not silently hash.
	if _, err := gcsHandler("").desiredStateHash("/h"); err == nil {
		t.Error("expected error for empty audience")
	}
}
