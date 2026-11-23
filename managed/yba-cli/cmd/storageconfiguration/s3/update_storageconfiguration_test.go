/*
 * Copyright (c) YugabyteDB, Inc.
 */

package s3

import (
	"maps"
	"strings"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestApplyS3Keys(t *testing.T) {
	const (
		maskedAccess = "AK******************"
		maskedSecret = "wJ**************************************"
		newAccess    = "AKIAI44QH8DHBEXAMPLE"
		newSecret    = "je7MtGbClwBF/2Zp9Utk/h3yCo8nvbEXAMPLEKEY"
		envAccess    = "AKIAIENVEXAMPLE00000"
		envSecret    = "envSecretEXAMPLEKEY"
	)
	// GET returns masked keys; YBA unmasks them on edit, so they must be sent back as is.
	stored := map[string]interface{}{
		util.AWSAccessKeyEnv:       maskedAccess,
		util.AWSSecretAccessKeyEnv: maskedSecret,
	}
	newKeys := map[string]interface{}{
		util.AWSAccessKeyEnv:       newAccess,
		util.AWSSecretAccessKeyEnv: newSecret,
	}
	tests := []struct {
		name            string
		accessKeyID     string
		secretAccessKey string
		keyFlagSet      bool
		updateCreds     bool
		wasIAM          bool
		isIAM           bool
		env             map[string]string
		want            map[string]interface{}
		wantErr         string
	}{
		{
			name: "rename only keeps stored keys",
			want: stored,
		},
		{
			name:            "both keys replace stored keys",
			accessKeyID:     newAccess,
			secretAccessKey: newSecret,
			keyFlagSet:      true,
			want:            newKeys,
		},
		{
			name:        "empty secret key",
			accessKeyID: newAccess,
			keyFlagSet:  true,
			wantErr:     "must both be non-empty",
		},
		{
			name:       "both keys set empty",
			keyFlagSet: true,
			wantErr:    "must both be non-empty",
		},
		{
			name:   "switching off IAM reads keys from env",
			wasIAM: true,
			env: map[string]string{
				util.AWSAccessKeyEnv:       envAccess,
				util.AWSSecretAccessKeyEnv: envSecret,
			},
			want: map[string]interface{}{
				util.AWSAccessKeyEnv:       envAccess,
				util.AWSSecretAccessKeyEnv: envSecret,
			},
		},
		{
			name:    "switching off IAM without keys in env",
			wasIAM:  true,
			wantErr: "Error getting AWS env credentials",
		},
		{
			name:            "switching off IAM with flag keys",
			accessKeyID:     newAccess,
			secretAccessKey: newSecret,
			keyFlagSet:      true,
			wasIAM:          true,
			want:            newKeys,
		},
		{
			name:        "update-credentials reads keys from env",
			updateCreds: true,
			env: map[string]string{
				util.AWSAccessKeyEnv:       envAccess,
				util.AWSSecretAccessKeyEnv: envSecret,
			},
			want: map[string]interface{}{
				util.AWSAccessKeyEnv:       envAccess,
				util.AWSSecretAccessKeyEnv: envSecret,
			},
		},
		{
			name:        "update-credentials without keys in env",
			updateCreds: true,
			wantErr:     "Error getting AWS env credentials",
		},
		{
			name:            "flag keys win over update-credentials",
			accessKeyID:     newAccess,
			secretAccessKey: newSecret,
			keyFlagSet:      true,
			updateCreds:     true,
			env: map[string]string{
				util.AWSAccessKeyEnv:       envAccess,
				util.AWSSecretAccessKeyEnv: envSecret,
			},
			want: newKeys,
		},
		{
			name:        "update-credentials with IAM is ignored",
			updateCreds: true,
			isIAM:       true,
			want:        map[string]interface{}{},
		},
		{
			name:  "switching to IAM drops stored keys",
			isIAM: true,
			want:  map[string]interface{}{},
		},
		{
			name:            "key flags with IAM",
			accessKeyID:     newAccess,
			secretAccessKey: newSecret,
			keyFlagSet:      true,
			isIAM:           true,
			wantErr:         "cannot be used with IAM",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			for _, k := range []string{util.AWSAccessKeyEnv, util.AWSSecretAccessKeyEnv,
				"AWS_ACCESS_KEY", "AWS_SECRET_KEY"} {
				t.Setenv(k, "")
			}
			for k, v := range tc.env {
				t.Setenv(k, v)
			}
			data := maps.Clone(stored)
			if tc.wasIAM {
				data = map[string]interface{}{}
			}
			err := applyS3Keys(
				data, tc.accessKeyID, tc.secretAccessKey,
				tc.keyFlagSet, tc.updateCreds, tc.wasIAM, tc.isIAM)
			if tc.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("got error %v, want one containing %q", err, tc.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if !maps.Equal(data, tc.want) {
				t.Fatalf("got %v, want %v", data, tc.want)
			}
		})
	}
}
