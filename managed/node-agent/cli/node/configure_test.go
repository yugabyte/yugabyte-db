// Copyright (c) YugaByte, Inc.

package node

import (
	"context"
	"fmt"
	"testing"
)

type testDisplayInterface struct {
	id   string
	name string
}

func (i testDisplayInterface) Id() string {
	return i.id
}

func (i testDisplayInterface) String() string {
	return fmt.Sprintf("ID: %s, Name: %s", i.id, i.name)
}

func (i testDisplayInterface) Name() string {
	return i.name
}

func TestDisplaceInterfaces(t *testing.T) {
	testData := []testDisplayInterface{
		{
			id:   "1",
			name: "display1",
		},
		{
			id:   "2",
			name: "display2",
		},
	}
	iFaces := displayInterfaces(context.TODO(), testData)
	for i, iFace := range iFaces {
		if iFace.Id() != testData[i].id {
			t.Fatalf("Expected %s, found %s", testData[i].id, iFace.Id())
		}
		if iFace.Name() != testData[i].name {
			t.Fatalf("Expected %s, found %s", testData[i].name, iFace.Name())
		}
	}
}
