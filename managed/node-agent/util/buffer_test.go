// Copyright (c) YugaByte, Inc.

package util

import "testing"

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
