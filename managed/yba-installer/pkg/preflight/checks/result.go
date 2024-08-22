package checks

import "strconv"

// Result of a check
type Result struct {
	Status Status
	Check  string
	Error  error
}

// MappedResults are results by their status
type MappedResults struct {
	Passed   []Result
	Warning  []Result
	Critical []Result
	Skipped  []Result
}

// NewMappedResults returns a map results pointer
func NewMappedResults() *MappedResults {
	return &MappedResults{
		Passed:   make([]Result, 0),
		Warning:  make([]Result, 0),
		Critical: make([]Result, 0),
		Skipped:  make([]Result, 0),
	}
}

// AddResult will add a new result to its correct status list
func (mr *MappedResults) AddResult(result Result) {
	switch result.Status {
	case StatusPassed:
		mr.Passed = append(mr.Passed, result)
	case StatusWarning:
		mr.Warning = append(mr.Warning, result)
	case StatusCritical:
		mr.Critical = append(mr.Critical, result)
	case StatusSkipped:
		mr.Skipped = append(mr.Skipped, result)
	default:
		panic("unknown status")
	}
}

// Status of a check
type Status int

const (
	// Possible status
	// Pass - check fully succeeds
	// Warning - check passed, but doesn't meet all best practices
	// Critical - check failed, we should not allow most operations
	// Skipped - check was not run, and should not be reported.
	StatusPassed Status = iota
	StatusWarning
	StatusCritical
	StatusSkipped
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
	case StatusSkipped:
		return "Skipped"
	default:
		return "unknown status value " + strconv.Itoa(int(s))
	}
}
