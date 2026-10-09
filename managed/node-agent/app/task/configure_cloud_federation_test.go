// Copyright (c) YugabyteDB, Inc.

package task

import (
	pb "node-agent/generated/service"
	"testing"
)

func gcsHandler(audience string) *ConfigureCloudFederation {
	return &ConfigureCloudFederation{
		param: &pb.ConfigureCloudFederationInput{
			SourceCloud: pb.ConfigureCloudFederationInput_AWS,
			TargetCloud: pb.ConfigureCloudFederationInput_GCP,
			Config: &pb.ConfigureCloudFederationInput_Gcs{
				Gcs: &pb.GcsConfig{Audience: audience},
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

func s3Handler(roleArn, audience, profile string) *ConfigureCloudFederation {
	return &ConfigureCloudFederation{
		param: &pb.ConfigureCloudFederationInput{
			SourceCloud: pb.ConfigureCloudFederationInput_GCP,
			TargetCloud: pb.ConfigureCloudFederationInput_AWS,
			Config: &pb.ConfigureCloudFederationInput_S3{
				S3: &pb.S3Config{
					RoleArn:     roleArn,
					Audience:    audience,
					ProfileName: profile,
				},
			},
		},
	}
}

// S3-on-GCP mirror of TestDesiredStateHash: stable for identical inputs, sensitive to every field
// that changes an on-node artifact (roleArn / audience / profileName / ybHome).
func TestDesiredStateHashS3(t *testing.T) {
	mustHash := func(h *ConfigureCloudFederation, ybHome string) string {
		t.Helper()
		s, err := h.desiredStateHash(ybHome)
		if err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		return s
	}

	roleA := "arn:aws:iam::123456789012:role/yb-federation"
	roleB := "arn:aws:iam::123456789012:role/yb-other"
	aud := "//iam.googleapis.com/projects/1/locations/global/workloadIdentityPools/p/providers/a"
	prof := "yb-cross-cloud-federation"

	base := mustHash(s3Handler(roleA, aud, prof), "/home/yugabyte")
	if again := mustHash(s3Handler(roleA, aud, prof), "/home/yugabyte"); base != again {
		t.Errorf("hash not stable for identical inputs: %s vs %s", base, again)
	}
	if h := mustHash(s3Handler(roleB, aud, prof), "/home/yugabyte"); base == h {
		t.Error("hash must change when roleArn changes")
	}
	if h := mustHash(s3Handler(roleA, aud+"b", prof), "/home/yugabyte"); base == h {
		t.Error("hash must change when audience changes")
	}
	if h := mustHash(s3Handler(roleA, aud, prof+"2"), "/home/yugabyte"); base == h {
		t.Error("hash must change when profileName changes")
	}
	if h := mustHash(s3Handler(roleA, aud, prof), "/other/home"); base == h {
		t.Error("hash must change when ybHome changes")
	}
}

// validateS3Inputs is the shell-injection guard: every field is interpolated into a rendered
// shell script / ~/.aws/config block, so metacharacter-bearing inputs must be rejected.
func TestValidateS3Inputs(t *testing.T) {
	roleA := "arn:aws:iam::123456789012:role/yb-federation"
	aud := "//iam.googleapis.com/projects/1/locations/global/workloadIdentityPools/p/providers/a"
	prof := "yb-cross-cloud-federation"

	if err := validateS3Inputs(s3Handler(roleA, aud, prof).param.GetS3()); err != nil {
		t.Errorf("valid inputs rejected: %v", err)
	}
	bad := []struct {
		name               string
		role, aud, profile string
	}{
		{"empty roleArn", "", aud, prof},
		{"malformed roleArn", "not-an-arn", aud, prof},
		{"roleArn injection", "arn:aws:iam::123456789012:role/x'; rm -rf /", aud, prof},
		{"empty audience", roleA, "", prof},
		{"audience injection", roleA, "aud\"; rm -rf /", prof},
		{"audience space", roleA, "aud with space", prof},
		{"empty profile", roleA, aud, ""},
		{"profile injection", roleA, aud, "p'; rm -rf /"},
	}
	for _, tc := range bad {
		if err := validateS3Inputs(&pb.S3Config{
			RoleArn: tc.role, Audience: tc.aud, ProfileName: tc.profile,
		}); err == nil {
			t.Errorf("%s: expected validation error, got nil", tc.name)
		}
	}
}
