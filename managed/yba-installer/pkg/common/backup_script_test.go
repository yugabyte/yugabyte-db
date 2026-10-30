package common

import (
	"os"
	"path/filepath"
	"testing"
)

func TestBackupScriptSupportsFips(t *testing.T) {
	dir := t.TempDir()
	withFlag := filepath.Join(dir, "with.sh")
	withoutFlag := filepath.Join(dir, "without.sh")
	if err := os.WriteFile(withFlag, []byte("case $1 in\n  --fips)\n    fips=true\n    ;;\nesac\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(withoutFlag, []byte("case $1 in\n  --verbose)\n    verbose=true\n    ;;\nesac\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if !BackupScriptSupportsFips(withFlag) {
		t.Errorf("expected --fips to be detected in %s", withFlag)
	}
	if BackupScriptSupportsFips(withoutFlag) {
		t.Errorf("expected --fips to be absent in %s", withoutFlag)
	}
	if BackupScriptSupportsFips(filepath.Join(dir, "missing.sh")) {
		t.Error("expected a missing script to be reported as unsupported")
	}
}
