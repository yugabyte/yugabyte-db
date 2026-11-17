/*
 * Copyright (c) YugabyteDB, Inc.
 */

package aws

import (
	"os"
	"os/exec"
	"strings"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestImageBundleSSHPort(t *testing.T) {
	if name := os.Getenv("TEST_SSH_PORT_FATAL"); name != "" {
		switch name {
		case "add":
			addAWSImageBundles([]string{
				"image-bundle-name=b1::ssh-user=ec2-user::ssh-port=abc",
			}, nil, nil, 0)
		case "edit":
			editAWSImageBundles([]string{
				"image-bundle-uuid=u1::ssh-port=70000",
			}, nil, sshBundle(22))
		case "create":
			buildAWSImageBundles([]string{
				"image-bundle-name=b1::ssh-user=ec2-user::ssh-port=0",
			}, nil, 0)
		default:
			t.Fatalf("unknown case %s", name)
		}
		return
	}

	got := addAWSImageBundles([]string{
		"image-bundle-name=b1::ssh-user=ec2-user::ssh-port=2222",
	}, nil, nil, 0)
	if port := sshPortValue(got[0]); port != 2222 {
		t.Errorf("add ssh port = %d, want 2222", port)
	}
	got = addAWSImageBundles([]string{
		"image-bundle-name=b1::ssh-user=ec2-user",
	}, nil, nil, 0)
	if port := sshPortValue(got[0]); port != 22 {
		t.Errorf("omitted add ssh port = %d, want 22", port)
	}

	got = buildAWSImageBundles([]string{
		"image-bundle-name=b1::ssh-user=ec2-user::ssh-port=2222",
	}, nil, 0)
	if port := sshPortValue(got[0]); port != 2222 {
		t.Errorf("create ssh port = %d, want 2222", port)
	}

	edited := editAWSImageBundles([]string{
		"image-bundle-uuid=u1::ssh-port=2222",
	}, nil, sshBundle(22))
	if port := sshPortValue(edited[0]); port != 2222 {
		t.Errorf("edit ssh port = %d, want 2222", port)
	}
	edited = editAWSImageBundles([]string{
		"image-bundle-uuid=u1::ssh-user=ec2-user",
	}, nil, sshBundle(22))
	if port := sshPortValue(edited[0]); port != 22 {
		t.Errorf("edit without ssh-port = %d, want 22", port)
	}

	rejectSSHPort(t, []sshPortCase{
		{"add", "invalid ssh-port abc for image bundle b1"},
		{"edit", "invalid ssh-port 70000 for image bundle u1"},
		{"create", "invalid ssh-port 0 for image bundle b1"},
	})
}

type sshPortCase struct {
	name string
	want string
}

func rejectSSHPort(t *testing.T, cases []sshPortCase) {
	t.Helper()
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			cmd := exec.Command(os.Args[0], "-test.run=^TestImageBundleSSHPort$")
			cmd.Env = append(os.Environ(), "TEST_SSH_PORT_FATAL="+tc.name)
			out, err := cmd.CombinedOutput()
			if err == nil {
				t.Fatalf("expected the command to exit, output %s", out)
			}
			if !strings.Contains(string(out), tc.want) {
				t.Fatalf("output %q, want %q", out, tc.want)
			}
		})
	}
}

func sshPortValue(bundle ybaclient.ImageBundle) int32 {
	port := bundle.GetDetails().SshPort
	if port == nil {
		return 0
	}
	return *port
}

func sshBundle(port int32) []ybaclient.ImageBundle {
	return []ybaclient.ImageBundle{{
		Uuid: util.GetStringPointer("u1"),
		Details: &ybaclient.ImageBundleDetails{
			SshPort: util.GetInt32Pointer(port),
		},
	}}
}
