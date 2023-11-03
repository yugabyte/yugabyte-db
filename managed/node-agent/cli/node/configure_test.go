// Copyright (c) YugaByte, Inc.

package node

import (
	"context"
	"testing"
)

type testDisplayInterface struct {
	name string
}

func (i *testDisplayInterface) ToString() string {
	return i.name
}

func TestDisplaceInterfaces(t *testing.T) {
	testData := []testDisplayInterface{
		{
			name: "display1",
		},
		{
			name: "display2",
		},
	}
	iFaces := displayInterfaces(context.TODO(), testData)
	for i, iFace := range iFaces {
		if iFace.String() != testData[i].name {
			t.Fatalf("Expected %s, found %s", testData[i].name, iFace)
		}
	}
}
