package testutils

import "testing"

var nextPort = 8500

func GetNextPort(t testing.TB) int {
	t.Helper()
	port := nextPort
	nextPort++
	if nextPort > 9000 {
		nextPort = 8500 // Reset to avoid overflow
	}
	return port
}
