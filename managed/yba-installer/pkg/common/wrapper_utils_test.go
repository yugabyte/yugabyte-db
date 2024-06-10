package common

import (
	"testing"
	"os"
)

const CustomDirPerms = 0700 + os.ModeDir

func TestCreateNestedDirectory(t *testing.T) {
	tmpDir := t.TempDir()
	testdir := tmpDir + "/food/bar/baz"
	err := MkdirAll(testdir, CustomDirPerms)
	if err != nil {
		t.Fatalf("error making directory %s: %s", testdir, err.Error())
	}
	ValidateDirectory(t, testdir, CustomDirPerms)
}

func TestCreateDirectory(t *testing.T) {
	tmpDir := t.TempDir()
	testdir := tmpDir + "/test"
	err := MkdirAll(testdir, CustomDirPerms)
	if err != nil {
		t.Fatalf("error making directory %s: %s", testdir, err.Error())
	}
	ValidateDirectory(t, testdir, CustomDirPerms)
}

func TestCreateExistingDirectory(t *testing.T) {
	testdir := t.TempDir()
	testDirInfo, _ := os.Stat(testdir)
	testDirPerms := testDirInfo.Mode()
	err := MkdirAll(testdir, CustomDirPerms)
	if err != nil {
		t.Fatalf("error making directory %s: %s", testdir, err.Error())
	}
	ValidateDirectory(t, testdir, testDirPerms)
}

func ValidateDirectory(t *testing.T, testdir string, perm os.FileMode) {
	fileInfo, err := os.Stat(testdir)
	if err != nil {
		t.Fatalf("error locating directory %s: %s", testdir, err.Error())
	}
	if fileInfo.Mode() != perm {
		t.Fatalf("directory %s has incorrect permissions. expected: %s actual: %s",
			testdir, perm, fileInfo.Mode())
	}
}
