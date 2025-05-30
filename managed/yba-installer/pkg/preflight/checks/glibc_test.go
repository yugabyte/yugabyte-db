package checks

import "testing"

func TestIsGlibcSupported(t *testing.T) {
	tests := []struct {
		version   string
		supported bool
	}{
		{"2.25", true},
		{"2.25.1", true},
		{"2.26", true},
		{"2.24", false},
		{"1.27", false},
		{"2.30", true},
		{"2.29", true},
	}

	for _, test := range tests {
		t.Run(test.version, func(t *testing.T) {
			if isGlibcVersionSupported(test.version) != test.supported {
				t.Errorf("Expected %s to be supported: %v", test.version, test.supported)
			}
		})
	}
}
