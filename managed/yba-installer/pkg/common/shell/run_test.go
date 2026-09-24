package shell

import (
	"strings"
	"testing"
)

// The progress filter has to hold back partial lines: the script writes its lock messages over
// whatever write boundaries the pipe happens to produce.
func TestProgressLoggerMatchesOnlyPrefixedWholeLines(t *testing.T) {
	var logged []string
	l := &progressLogger{prefix: "[backup-lock] "}
	l.log = func(msg string) { logged = append(logged, msg) }

	writes := []string{
		"Creating Postgres DB backup...\n[backup-lock] Another platform ",
		"backup is in progress: pid 7.\nDone\n[backup-lock] Still waiting (30s)...\n",
		"[backup-lock] partial line, no newline yet",
	}
	for _, w := range writes {
		if n, err := l.Write([]byte(w)); err != nil || n != len(w) {
			t.Fatalf("Write(%q) = %d, %v", w, n, err)
		}
	}

	want := []string{
		"Another platform backup is in progress: pid 7.",
		"Still waiting (30s)...",
	}
	if strings.Join(logged, "|") != strings.Join(want, "|") {
		t.Errorf("logged %q, want %q", logged, want)
	}
}
