package age

import (
	"bytes"
	"fmt"
)

type AgeError struct {
	cause error
	msg   string
}

func (e *AgeError) Error() string {
	if e.cause != nil {
		return fmt.Sprintf("%s >> Cause:%s", e.msg, e.cause.Error())
	}
	return e.msg
}

type AgeParseError struct {
	msg    string
	errors []string
}

func (e *AgeParseError) Error() string {
	var buf bytes.Buffer
	buf.WriteString(e.msg)
	buf.WriteString(" >> Causes:\n")
	for _, err := range e.errors {
		buf.WriteString(err)
		buf.WriteString("\n")
	}
	return buf.String()
}
