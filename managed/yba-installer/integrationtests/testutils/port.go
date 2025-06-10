package testutils

import "testing"

var nextPort = 8500

func GetNextPort(tb testing.TB) int {
	tb.Helper()
	port := nextPort
	nextPort++
	if nextPort > 9000 {
		nextPort = 8500 // Reset to avoid overflow
	}
	return port
}
