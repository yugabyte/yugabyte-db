//go:build nolicense

package pubkey

// ValidationRequired shows if validation needs to be performed based on the build type.
const ValidationRequired bool = false

// Validate is a dev-build validation, which no-ops and returns true.
func Validate(hash, signature []byte) bool {
	return true
}
