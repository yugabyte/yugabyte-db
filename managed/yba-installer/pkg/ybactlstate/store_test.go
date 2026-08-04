package ybactlstate

import (
	"bytes"
	"io"
)

type mockFilesystem struct {
	OpenErr   error
	CreateErr error

	OpenBuffer   *bytes.Buffer
	CreateBuffer *bytes.Buffer
}

func (m mockFilesystem) Open(fp string) (io.Reader, error) {
	return m.OpenBuffer, m.OpenErr
}

func (m mockFilesystem) Create(fp string) (io.Writer, error) {
	return m.CreateBuffer, m.CreateErr
}

// Returns a reset function that should be defered
func patchFS() (*mockFilesystem, func()) {
	old_fs := fs
	ms := &mockFilesystem{}
	fs = ms
	return ms, func() { fs = old_fs }
}
