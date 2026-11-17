/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcp

import (
	"os"
	"os/exec"
	"strings"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestImageBundleSSHPort(t *testing.T) {
	const addBundle = "image-bundle-name=b1::machine-image=img::ssh-user=centos"
	if name := os.Getenv("TEST_SSH_PORT_FATAL"); name != "" {
		switch name {
		case "add":
			addGCPImageBundles([]string{addBundle + "::ssh-port=abc"}, nil)
		case "edit":
			editGCPImageBundles([]string{
				"image-bundle-uuid=u1::ssh-port=70000",
			}, sshBundle(22))
		case "create":
			buildGCPImageBundles([]string{addBundle + "::ssh-port=0"})
		default:
			t.Fatalf("unknown case %s", name)
		}
		return
	}

	got := addGCPImageBundles([]string{addBundle + "::ssh-port=2222"}, nil)
	if port := sshPortValue(got[0]); port != 2222 {
		t.Errorf("add ssh port = %d, want 2222", port)
	}
	got = addGCPImageBundles([]string{addBundle}, nil)
	if port := sshPortValue(got[0]); port != 22 {
		t.Errorf("omitted add ssh port = %d, want 22", port)
	}

	got = buildGCPImageBundles([]string{addBundle + "::ssh-port=2222"})
	if port := sshPortValue(got[0]); port != 2222 {
		t.Errorf("create ssh port = %d, want 2222", port)
	}

	edited := editGCPImageBundles([]string{
		"image-bundle-uuid=u1::ssh-port=2222",
	}, sshBundle(22))
	if port := sshPortValue(edited[0]); port != 2222 {
		t.Errorf("edit ssh port = %d, want 2222", port)
	}
	edited = editGCPImageBundles([]string{
		"image-bundle-uuid=u1::ssh-user=centos",
	}, sshBundle(22))
	if port := sshPortValue(edited[0]); port != 22 {
		t.Errorf("edit without ssh-port = %d, want 22", port)
	}

	for _, tc := range []struct{ name, want string }{
		{"add", "invalid ssh-port abc for image bundle b1"},
		{"edit", "invalid ssh-port 70000 for image bundle u1"},
		{"create", "invalid ssh-port 0 for image bundle b1"},
	} {
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
