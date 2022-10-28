// Copyright (c) YugaByte, Inc.

package server

import (
	"context"
	"log"
	pb "node-agent/generated/service"
	"os"
	"testing"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

var (
	server     *RPCServer
	clientCtx  context.Context
	dialOpts   []grpc.DialOption
	serverAddr = "localhost:0"
)

// TestMain is invoked before the tests.
func TestMain(m *testing.M) {
	var err error
	ctx := Context()
	cancelFunc = CancelFunc()
	dialOpts = append(dialOpts, grpc.WithTransportCredentials(insecure.NewCredentials()))
	server, err = NewRPCServer(ctx, serverAddr, false)
	if err != nil {
		panic(err)
	}
	// Update with the actual address.
	serverAddr = server.Addr()
	log.Printf("Listening to server address %s", serverAddr)
	code := m.Run()
	server.Stop()
	cancelFunc()
	os.Exit(code)
}

// Server test starts here.
func TestPing(t *testing.T) {
	conn, err := grpc.Dial(serverAddr, dialOpts...)
	if err != nil {
		log.Fatalf("Fail to dial: %v", err)
	}
	defer conn.Close()
	client := pb.NewNodeAgentClient(conn)
	req := pb.PingRequest{Data: "Hello"}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	res, err := client.Ping(ctx, &req)
	if err != nil {
		t.Fatal(err)
	}
	if res.Data != "Hello" {
		t.Fatalf("Expected 'Hello', found '%s'", res.Data)
	}
}
