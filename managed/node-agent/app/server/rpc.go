// Copyright (c) YugaByte, Inc.

package server

import (
	"context"
	"crypto/tls"
	"fmt"
	"net"
	pb "node-agent/generated/service"
	"node-agent/util"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
)

type RPCServer struct {
	gServer *grpc.Server
	isTLS   bool
}

func NewRPCServer(ctx context.Context, host, port string, isTLS bool) (*RPCServer, error) {
	serverOpts := []grpc.ServerOption{}
	if isTLS {
		tlsCredentials, err := loadTLSCredentials()
		if err != nil {
			util.FileLogger().Errorf("Error in loading TLS credentials: %s", err)
			return nil, err
		}
		serverOpts = append(serverOpts, grpc.Creds(tlsCredentials))
	}
	hostPort := fmt.Sprintf("%s:%s", host, port)
	listener, err := net.Listen("tcp", hostPort)
	if err != nil {
		util.FileLogger().Errorf("Failed to listen to %s: %v", hostPort, err)
		return nil, err
	}
	gServer := grpc.NewServer(serverOpts...)
	server := &RPCServer{gServer: gServer, isTLS: isTLS}
	pb.RegisterNodeAgentServer(gServer, server)
	go func() {
		if err := gServer.Serve(listener); err != nil {
			util.FileLogger().Errorf("Failed to start RPC server: %v", err)
		}
	}()
	return server, nil
}

func loadTLSCredentials() (credentials.TransportCredentials, error) {
	config := util.CurrentConfig()
	certFilePath := util.ServerCertPath(config)
	keyFilepath := util.ServerKeyPath(config)
	serverCert, err := tls.LoadX509KeyPair(certFilePath, keyFilepath)
	if err != nil {
		return nil, err
	}
	tlsConfig := &tls.Config{
		Certificates: []tls.Certificate{serverCert},
		ClientAuth:   tls.NoClientCert,
	}
	return credentials.NewTLS(tlsConfig), nil
}

func (server *RPCServer) Stop() {
	if server.gServer != nil {
		server.gServer.GracefulStop()
	}
	server.gServer = nil
}

/* Implementation of gRPC methods start here. */

func (s *RPCServer) Ping(ctx context.Context, in *pb.PingRequest) (*pb.PingResponse, error) {
	util.FileLogger().Debugf("Received: %v", in.Data)
	return &pb.PingResponse{Data: in.Data}, nil
}

/* End of gRPC methods. */
