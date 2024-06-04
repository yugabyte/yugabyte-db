// Copyright 2016 The CMux Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.

// The file is modified for YugaByte, Inc.

package cmux

import (
	"errors"
	"fmt"
	"io"
	"net"
	"node-agent/util"
	"sync"
)

// Matcher matches a connection based on its content.
type Matcher func(io.Reader) bool

// ErrorHandler handles an error and returns whether
// the mux should continue serving the listener.
type ErrorHandler func(error) bool

// ErrNotMatched is returned whenever a connection is not matched by any of
// the matchers registered in the multiplexer.
type ErrNotMatched struct {
	c net.Conn
}

// Error implements error.
func (e ErrNotMatched) Error() string {
	return fmt.Sprintf("mux: connection %v not matched by an matcher",
		e.c.RemoteAddr())
}

// Temporary implements the net.Error interface.
func (e ErrNotMatched) Temporary() bool { return true }

// Timeout implements the net.Error interface.
func (e ErrNotMatched) Timeout() bool { return false }

type errListenerClosed string

func (e errListenerClosed) Error() string   { return string(e) }
func (e errListenerClosed) Temporary() bool { return false }
func (e errListenerClosed) Timeout() bool   { return false }

var (
	// ErrListenerClosed is returned from muxListener.Accept when the underlying
	// listener is closed.
	ErrListenerClosed = errListenerClosed("mux: listener closed")

	// ErrServerClosed is returned from muxListener.Accept when mux server is closed.
	ErrServerClosed = errors.New("mux: server closed")
)

// New instantiates a new connection multiplexer.
func New(l net.Listener) CMux {
	return &cMux{
		root:   l,
		bufLen: 1024,
		errh:   func(error) bool { return true },
		donec:  make(chan struct{}),
	}
}

// CMux is a multiplexer for network connections.
type CMux interface {
	// Match returns a net.Listener that sees (i.e., accepts) only
	// the connections matched by at least one of the matcher.
	//
	// The order used to call Match determines the priority of matchers.
	Match(...Matcher) net.Listener
	// Serve starts multiplexing the listener. Serve blocks and perhaps
	// should be invoked concurrently within a go routine.
	Serve() error
	// Closes cmux server and stops accepting any connections on listener
	Close()
	// HandleError registers an error handler that handles listener errors.
	HandleError(ErrorHandler)
}

type matchersListener struct {
	listener muxListener
	matchers []Matcher
}

type cMux struct {
	root       net.Listener
	bufLen     int
	errh       ErrorHandler
	mListeners []matchersListener
	donec      chan struct{}
	mu         sync.Mutex
}

// Match registers the matchers and returns the corresponding listener.
func (m *cMux) Match(matchers ...Matcher) net.Listener {
	ml := matchersListener{
		listener: muxListener{
			Listener: m.root,
			connc:    make(chan net.Conn, m.bufLen),
			donec:    make(chan struct{}),
		},
	}
	ml.matchers = append(ml.matchers, matchers...)
	m.mListeners = append(m.mListeners, ml)
	return ml.listener
}

// Serve serves the root listener.
func (m *cMux) Serve() error {
	var wg sync.WaitGroup
	defer func() {
		m.closeDoneChans()
		wg.Wait()

		for _, ml := range m.mListeners {
			close(ml.listener.connc)
			// Drain the connections enqueued for the listener.
			for c := range ml.listener.connc {
				c.Close()
			}
		}
	}()

	for {
		c, err := m.root.Accept()
		if err != nil {
			if !m.handleErr(err) {
				return err
			}
			continue
		}

		wg.Add(1)
		go m.serve(c, m.donec, &wg)
	}
}

func (m *cMux) serve(c net.Conn, donec <-chan struct{}, wg *sync.WaitGroup) {
	defer wg.Done()
	muc := newMuxConn(c)
	for _, lms := range m.mListeners {
		for _, matcher := range lms.matchers {
			matched := matcher(muc)
			muc.reset()
			if matched {
				muc.disableReset()
				select {
				case lms.listener.connc <- muc:
				case <-donec:
					c.Close()
				}
				return
			}
		}
	}

	c.Close()
	err := ErrNotMatched{c: c}
	if !m.handleErr(err) {
		m.root.Close()
	}
}

// Close closes the mux.
func (m *cMux) Close() {
	m.closeDoneChans()
}

func (m *cMux) closeDoneChans() {
	m.mu.Lock()
	defer m.mu.Unlock()

	select {
	case <-m.donec:
		// Already closed. Don't close again
	default:
		close(m.donec)
	}
	for _, ml := range m.mListeners {
		select {
		case <-ml.listener.donec:
			// Already closed. Don't close again
		default:
			close(ml.listener.donec)
		}
	}
}

// HandleError sets the error handler.
func (m *cMux) HandleError(h ErrorHandler) {
	m.errh = h
}

func (m *cMux) handleErr(err error) bool {
	if !m.errh(err) {
		return false
	}

	if ne, ok := err.(net.Error); ok {
		return ne.Temporary()
	}

	return false
}

type muxListener struct {
	net.Listener
	connc chan net.Conn
	donec chan struct{}
}

func (l muxListener) Accept() (net.Conn, error) {
	select {
	case c, ok := <-l.connc:
		if !ok {
			return nil, ErrListenerClosed
		}
		return c, nil
	case <-l.donec:
		return nil, ErrServerClosed
	}
}

// MuxConn wraps a net.Conn and provides transparent sniffing of connection data.
type muxConn struct {
	net.Conn
	buffer util.ResettableBuffer
}

func newMuxConn(c net.Conn) *muxConn {
	return &muxConn{
		Conn:   c,
		buffer: util.NewResettableBuffer(c),
	}
}

func (m *muxConn) reset() {
	m.buffer.Reset()
}

func (m *muxConn) disableReset() {
	m.buffer.DisableReset()
}

// Read implements io.Reader.
func (m *muxConn) Read(p []byte) (int, error) {
	return m.buffer.Read(p)
}
