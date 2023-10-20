// Copyright (c) YugaByte, Inc.

package util

import (
	"bytes"
	"io"
	"sync"
)

// Buffer is a circular buffer which extends io.Writer.
type Buffer interface {
	io.Writer
	// Consume advances the buffer position by size.
	Consume(size int)
	// Len returns the length of the unread bytes.
	Len() int
	// String returns the string of the unread bytes.
	String() string
	// StringWithLen returns the string and size of the unread bytes.
	StringWithLen() (string, int)
}

// ResettableBuffer is a buffer which can be reset to the beginning of the buffer.
type ResettableBuffer interface {
	io.Reader
	Reset()
	DisableReset()
}

// simpleBuffer implements Buffer.
type simpleBuffer struct {
	maxCap int
	mutex  *sync.Mutex
	buffer *bytes.Buffer
}

// resettableBuffer is a resettable bufer which extends io.Reader.
type resettableBuffer struct {
	source       io.Reader
	readBuffer   *bytes.Buffer
	disableReset bool
}

// NewBuffer creates an instance of Buffer.
func NewBuffer(maxCap int) Buffer {
	return &simpleBuffer{maxCap: maxCap, mutex: &sync.Mutex{}, buffer: &bytes.Buffer{}}
}

// Write writes to the buffer.
func (p *simpleBuffer) Write(ba []byte) (int, error) {
	p.mutex.Lock()
	defer p.mutex.Unlock()
	n, err := p.buffer.Write(ba)
	if err != nil {
		return n, err
	}
	excess := p.buffer.Len() - p.maxCap
	if excess > 0 {
		// Consume the excess (FIFO).
		p.buffer.Next(excess)
	}
	return n, nil
}

// Len implements Buffer method.
func (p *simpleBuffer) Len() int {
	p.mutex.Lock()
	defer p.mutex.Unlock()
	// Keep only the data from the offset.
	return p.buffer.Len()
}

// Consume implements Buffer method.
func (p *simpleBuffer) Consume(size int) {
	p.mutex.Lock()
	defer p.mutex.Unlock()
	// Keep only the data from the offset.
	p.buffer.Next(size)
}

// String implements Buffer method.
func (p *simpleBuffer) String() string {
	p.mutex.Lock()
	defer p.mutex.Unlock()
	return p.buffer.String()
}

// StringWithLen implements Buffer method.
func (p *simpleBuffer) StringWithLen() (string, int) {
	p.mutex.Lock()
	defer p.mutex.Unlock()
	return p.buffer.String(), p.buffer.Len()
}

// NewResettableBuffer returns a new instance of resettable buffer reader.
func NewResettableBuffer(source io.Reader) ResettableBuffer {
	return &resettableBuffer{source: source, readBuffer: &bytes.Buffer{}, disableReset: false}
}

// Reset resets the buffer.
func (rb *resettableBuffer) Reset() {
	if !rb.disableReset && rb.readBuffer.Len() > 0 {
		readBuffer := &bytes.Buffer{}
		rb.readBuffer.WriteTo(readBuffer)
		rb.source = io.MultiReader(readBuffer, rb.source)
	}
}

// DisableReset disables further reset.
func (rb *resettableBuffer) DisableReset() {
	rb.disableReset = true
}

// Read reads bytes from the buffer.
func (rb *resettableBuffer) Read(p []byte) (int, error) {
	if rb.disableReset {
		return rb.source.Read(p)
	}
	return io.TeeReader(rb.source, rb.readBuffer).Read(p)
}
