package ybactlstate

/*
 * Define a mock `stateFileSystem` to use for testing the state
 */

import (
	"io"
)

type mockFS struct {
	WriteBuffer io.Writer
}

func (mockFS) Open(fp string) (io.Reader, error) {
	return nil, nil
}

func (mf mockFS) Create(fp string) (io.Writer, error) {
	return mf.WriteBuffer, nil
}

type devNullWriter struct{}

func (*devNullWriter) Write(p []byte) (int, error) {
	return len(p), nil
}
