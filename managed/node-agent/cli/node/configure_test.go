// Copyright (c) YugaByte, Inc.

package node

import "testing"

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
	iFaces := displayInterfaces(testData)
	for i, iFace := range iFaces {
		if iFace.ToString() != testData[i].name {
			t.Fatalf("Expected %s, found %s", testData[i].name, iFace.ToString())
		}
	}
}
