package checks

import "testing"

func TestPgcryptoError(t *testing.T) {
	tests := []struct {
		name                         string
		available, installed, create bool
		wantErr                      bool
	}{
		{"installed", true, true, false, false},
		{"creatable", true, false, true, false},
		{"not available", false, false, true, true},
		{"no create privilege", true, false, false, true},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			err := pgcryptoError(test.available, test.installed, test.create, "yba")
			if (err != nil) != test.wantErr {
				t.Errorf("pgcryptoError(available=%v, installed=%v, canCreate=%v) = %v, wantErr %v",
					test.available, test.installed, test.create, err, test.wantErr)
			}
		})
	}
}
