package common

import (
	"os"
	"path/filepath"
	"slices"
	"testing"
)

func TestPruneNodeAgentPackages(t *testing.T) {
	files := []string{
		"node_agent-2.31.0.0-b321-linux-amd64.tar.gz",
		"node_agent-2.31.0.0-b321-linux-arm64.tar.gz",
		"node_agent-2.31.0.0-b3210-linux-amd64.tar.gz",
		"node_agent-2.31.0.0-b365-linux-amd64.tar.gz",
		"node_agent-2.29.0.0-b484-linux-amd64.tar.gz",
		"ybc-2.2.0.4-b10-linux-x86_64.tar.gz",
	}
	tests := []struct {
		name        string
		keepVersion string
		want        []string
	}{
		{
			name:        "keeps only the given version",
			keepVersion: "2.31.0.0-b321",
			want: []string{
				"node_agent-2.31.0.0-b321-linux-amd64.tar.gz",
				"node_agent-2.31.0.0-b321-linux-arm64.tar.gz",
				"ybc-2.2.0.4-b10-linux-x86_64.tar.gz",
			},
		},
		{
			name:        "deletes nothing when no package matches",
			keepVersion: "2.31.0.0-PRE_RELEASE",
			want:        files,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			dir := t.TempDir()
			for _, f := range files {
				if err := os.WriteFile(filepath.Join(dir, f), nil, 0644); err != nil {
					t.Fatal(err)
				}
			}
			PruneNodeAgentPackages(dir, tc.keepVersion)
			entries, err := os.ReadDir(dir)
			if err != nil {
				t.Fatal(err)
			}
			var got []string
			for _, e := range entries {
				got = append(got, e.Name())
			}
			want := slices.Clone(tc.want)
			slices.Sort(want)
			if !slices.Equal(got, want) {
				t.Errorf("got %v, want %v", got, want)
			}
		})
	}
}
