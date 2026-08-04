package ybactlstate

import (
	"io"
	"os"
)

type stateFilesystem interface {
	Open(fp string) (io.Reader, error)
	Create(fp string) (io.Writer, error)
}

var fs stateFilesystem

// osfs implements the stateFilesystem interface using os library
type osfs struct{}

func (osfs) Open(fp string) (io.Reader, error) {
	return os.Open(fp)
}

func (osfs) Create(fp string) (io.Writer, error) {
	return os.Create(fp)
}

func init() {
	fs = osfs{}
}
