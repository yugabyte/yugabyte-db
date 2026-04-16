/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"io"
	"sync"

	"github.com/apex/log"
	"github.com/go-logfmt/logfmt"
)

const (
	timeFormat = "2006-01-02T15:04:05.999Z"
)

// LogHandler is a custom wrapper over apex logfmt handler.
type LogHandler struct {
	mu  *sync.Mutex
	enc *logfmt.Encoder
}

// NewLogHandler returns a new log handler that writes to w.
func NewLogHandler(w io.Writer) *LogHandler {
	return &LogHandler{
		mu:  &sync.Mutex{},
		enc: logfmt.NewEncoder(w),
	}
}

// HandleLog implements log.Handler.
func (h *LogHandler) HandleLog(e *log.Entry) error {
	names := e.Fields.Names()

	h.mu.Lock()
	defer h.mu.Unlock()
	h.enc.EncodeKeyval("timestamp", e.Timestamp.UTC().Format(timeFormat))
	h.enc.EncodeKeyval("level", e.Level.String())
	h.enc.EncodeKeyval("message", e.Message)

	for _, name := range names {
		h.enc.EncodeKeyval(name, e.Fields.Get(name))
	}

	h.enc.EndRecord()

	return nil
}
