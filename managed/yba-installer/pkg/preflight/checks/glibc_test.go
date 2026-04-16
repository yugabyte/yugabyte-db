package checks

import "testing"

func TestIsGlibcSupported(t *testing.T) {
	tests := []struct {
		version   string
		supported bool
	}{
		{"2.25", false},
		{"2.25.1", false},
		{"2.26", false},
		{"2.27.9", false},
		{"2.28", true},
		{"2.28.1", true},
		{"2.29", true},
		{"2.30", true},
	}

	for _, test := range tests {
		t.Run(test.version, func(t *testing.T) {
			if isGlibcVersionSupported(test.version) != test.supported {
				t.Errorf("Expected %s to be supported: %v", test.version, test.supported)
			}
		})
	}
}
