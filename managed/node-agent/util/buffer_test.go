// Copyright (c) YugaByte, Inc.

package util

import (
	"bytes"
	"testing"
)

func TestBuffer(t *testing.T) {
	buffer := NewBuffer(3)
	buffer.Write([]byte("h"))
	buffer.Write([]byte("e"))
	buffer.Write([]byte("l"))
	buffer.Write([]byte("l"))
	buffer.Write([]byte("o"))
	data, size := buffer.StringWithLen()
	if string(data) != "llo" {
		t.Fatalf("expected 'llo', found %s", string(data))
	}
	if size != 3 {
		t.Fatalf("expected 3, found %d", size)
	}
	buffer.Consume(1)
	data, size = buffer.StringWithLen()
	if string(data) != "lo" {
		t.Fatalf("expected 'lo', found %s", string(data))
	}
	if size != 2 {
		t.Fatalf("expected 2, found %d", size)
	}
	buffer.Consume(2)
	data, size = buffer.StringWithLen()
	if string(data) != "" {
		t.Fatalf("expected '', found %s", string(data))
	}
	if size != 0 {
		t.Fatalf("expected 0, found %d", size)
	}
	buffer.Consume(1)
	data, size = buffer.StringWithLen()
	if string(data) != "" {
		t.Fatalf("expected '', found %s", string(data))
	}
	if size != 0 {
		t.Fatalf("expected 0, found %d", size)
	}
}

func TestResettableBuffer(t *testing.T) {
	source := &bytes.Buffer{}
	source.Write([]byte("hello"))
	buffer := NewResettableBuffer(source)
	data := make([]byte, 1)
	_, err := buffer.Read(data)
	if err != nil {
		t.Fatal(err)
	}
	if "h" != string(data) {
		t.Logf("Expected h, found %s", string(data))
	}
	_, err = buffer.Read(data)
	if err != nil {
		t.Fatal(err)
	}
	if "e" != string(data) {
		t.Logf("Expected e, found %s", string(data))
	}
	buffer.Reset()
	_, err = buffer.Read(data)
	if err != nil {
		t.Fatal(err)
	}
	if "h" != string(data) {
		t.Logf("Expected h, found %s after reset", string(data))
	}
	buffer.Reset()
	_, err = buffer.Read(data)
	if err != nil {
		t.Fatal(err)
	}
	if "h" != string(data) {
		t.Logf("Expected h, found %s after reset", string(data))
	}
}
