/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"encoding/json"
	"testing"
	"time"
)

func TestScheduleUnmarshalNextExpectedTask(t *testing.T) {
	tests := []struct {
		name string
		body string
		want time.Time
	}{
		{
			name: "iso string as sent by current YBA",
			body: `{"nextExpectedTask":"2026-05-17T14:29:26Z"}`,
			want: time.Date(2026, 5, 17, 14, 29, 26, 0, time.UTC),
		},
		{
			name: "epoch millis as sent by older YBA",
			body: `{"nextExpectedTask":1779028166000}`,
			want: time.UnixMilli(1779028166000),
		},
		{
			name: "offset and fractional seconds",
			body: `{"nextExpectedTask":"2026-05-17T14:29:26.500+05:30"}`,
			want: time.Date(2026, 5, 17, 8, 59, 26, 500000000, time.UTC),
		},
		{
			name: "null",
			body: `{"nextExpectedTask":null}`,
			want: time.Time{},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			schedule := Schedule{}
			if err := json.Unmarshal([]byte(tt.body), &schedule); err != nil {
				t.Fatalf("unmarshal %s: %v", tt.body, err)
			}
			got := schedule.GetNextScheduleTaskTime()
			if !got.Equal(tt.want) {
				t.Errorf("got %v, want %v", got.Time, tt.want)
			}
		})
	}
}

func TestScheduleUnmarshalBadNextExpectedTask(t *testing.T) {
	schedule := Schedule{}
	err := json.Unmarshal([]byte(`{"nextExpectedTask":"not a time"}`), &schedule)
	if err == nil {
		t.Fatal("expected an error for an unparseable time")
	}
}
