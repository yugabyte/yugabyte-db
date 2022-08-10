// Copyright (c) YugaByte, Inc.

package executor

//Struct that stores the result of a task executor handler.
type Result struct {
	err    error
	status string
	data   any
}

func NewEmptyResult() *Result {
	return &Result{}
}
func NewResult(err error, status string, data any) *Result {
	return &Result{err: err, status: status, data: data}
}

func (r Result) Err() error {
	return r.err
}

func (r Result) Data() any {
	return r.data
}

func (r *Result) SetErr(err error) {
	r.err = err
}

func (r *Result) SetStatus(status string) {
	r.status = status
}

func (r *Result) SetData(dto any) {
	r.data = dto
}
