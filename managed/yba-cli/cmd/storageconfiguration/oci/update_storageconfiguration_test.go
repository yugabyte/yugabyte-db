/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"maps"
	"strings"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestApplyOCIS3Keys(t *testing.T) {
	const (
		oldAccess = "AKIAIOSFODNN7EXAMPLE"
		oldSecret = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY"
		newAccess = "AKIAI44QH8DHBEXAMPLE"
		newSecret = "je7MtGbClwBF/2Zp9Utk/h3yCo8nvbEXAMPLEKEY"
		envAccess = "AKIAIENVEXAMPLE00000"
		envSecret = "envSecretEXAMPLEKEY"
	)
	stored := map[string]interface{}{
		util.OCIS3AccessKeyIDField:     oldAccess,
		util.OCIS3SecretAccessKeyField: oldSecret,
	}
	newKeys := map[string]interface{}{
		util.OCIS3AccessKeyIDField:     newAccess,
		util.OCIS3SecretAccessKeyField: newSecret,
	}
	tests := []struct {
		name            string
		accessKeyID     string
		secretAccessKey string
		keyFlagSet      bool
		wasIAM          bool
		env             map[string]string
		want            map[string]interface{}
		wantErr         string
	}{
		{
			name: "no key flags keeps stored keys",
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
			name:            "empty access key",
			secretAccessKey: newSecret,
			keyFlagSet:      true,
			wantErr:         "must both be non-empty",
		},
		{
			name:            "whitespace secret key",
			accessKeyID:     newAccess,
			secretAccessKey: "  ",
			keyFlagSet:      true,
			wantErr:         "must both be non-empty",
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
				util.OCIS3AccessKeyIDField:     envAccess,
				util.OCIS3SecretAccessKeyField: envSecret,
			},
			want: map[string]interface{}{
				util.OCIS3AccessKeyIDField:     envAccess,
				util.OCIS3SecretAccessKeyField: envSecret,
			},
		},
		{
			name:    "switching off IAM without keys in env",
			wasIAM:  true,
			env:     map[string]string{},
			wantErr: "environment variables must both be set",
		},
		{
			name:            "switching off IAM with flag keys",
			accessKeyID:     newAccess,
			secretAccessKey: newSecret,
			keyFlagSet:      true,
			wasIAM:          true,
			want:            newKeys,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			t.Setenv(util.OCIS3AccessKeyIDField, "")
			t.Setenv(util.OCIS3SecretAccessKeyField, "")
			for k, v := range tc.env {
				t.Setenv(k, v)
			}
			data := maps.Clone(stored)
			if tc.wasIAM {
				data = map[string]interface{}{}
			}
			err := applyOCIS3Keys(
				data, tc.accessKeyID, tc.secretAccessKey, tc.keyFlagSet, tc.wasIAM)
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
