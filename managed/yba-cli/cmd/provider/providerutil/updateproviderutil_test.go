package providerutil

import (
	"os"
	"os/exec"
	"testing"
)

func TestEditClearsInstanceTemplateOnlyWhenAllowed(t *testing.T) {
	if os.Getenv("TEST_EMPTY_INSTANCE_TEMPLATE") == "1" {
		BuildRegionMapFromString("region-name=us-sanjose-1::instance-template=", "edit")
		return
	}

	cleared := BuildRegionMapFromStringAllowingClearInstanceTemplate(
		"region-name=us-sanjose-1::vnet=v1::instance-template=", "edit")
	if v, ok := cleared["instance-template"]; !ok || v != "" {
		t.Fatalf("empty instance-template on an allowing edit: got %q present=%v", v, ok)
	}

	kept := BuildRegionMapFromStringAllowingClearInstanceTemplate(
		"region-name=us-sanjose-1::vnet=v1", "edit")
	if _, ok := kept["instance-template"]; ok {
		t.Fatal("omitted instance-template should stay unset")
	}

	set := BuildRegionMapFromString("region-name=r::instance-template=abc", "edit")
	if set["instance-template"] != "abc" {
		t.Fatalf("non-empty instance-template: got %q", set["instance-template"])
	}

	cmd := exec.Command(os.Args[0], "-test.run=TestEditClearsInstanceTemplateOnlyWhenAllowed")
	cmd.Env = append(os.Environ(), "TEST_EMPTY_INSTANCE_TEMPLATE=1")
	if err := cmd.Run(); err == nil {
		t.Fatal("empty instance-template on a provider that cannot clear it should fail")
	}
}
