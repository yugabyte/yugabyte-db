package checks

import "strconv"

// Result of a check
type Result struct {
	Status Status
	Check  string
	Error  error
}

// Status of a check
type Status int

const (
	// Possible status
	// Pass - check fully succeeds
	// Warning - check passed, but doesn't meet all best practices
	// Critical - check failed, we should not allow most operations
	StatusPassed Status = iota
	StatusWarning
	StatusCritical
	maxStatus // marker for the highest allowed status value, could be used for validation later
)

// String returns the status as a string
func (s Status) String() string {
	switch s {
	case StatusCritical:
		return "Critical"
	case StatusWarning:
		return "Warning"
	case StatusPassed:
		return "Pass"
	default:
		return "unknown status value " + strconv.Itoa(int(s))
	}
}
