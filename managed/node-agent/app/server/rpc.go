// Copyright (c) YugaByte, Inc.

package server

import (
	"bufio"
	"context"
	"crypto/tls"
	"io"
	"net"
	"node-agent/app/task"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/status"
)

type RPCServer struct {
	addr    net.Addr
	gServer *grpc.Server
	isTLS   bool
}

func (server *RPCServer) Addr() string {
	return server.addr.String()
}

func NewRPCServer(ctx context.Context, addr string, isTLS bool) (*RPCServer, error) {
	serverOpts := []grpc.ServerOption{}
	if isTLS {
		tlsCredentials, err := loadTLSCredentials()
		if err != nil {
			util.FileLogger().Errorf("Error in loading TLS credentials: %s", err)
			return nil, err
		}
		authenticator := Authenticator{util.CurrentConfig()}
		serverOpts = append(serverOpts, grpc.Creds(tlsCredentials))
		serverOpts = append(serverOpts, grpc.UnaryInterceptor(authenticator.UnaryInterceptor()))
		serverOpts = append(serverOpts, grpc.StreamInterceptor(authenticator.StreamInterceptor()))
	}
	listener, err := net.Listen("tcp", addr)
	if err != nil {
		util.FileLogger().Errorf("Failed to listen to %s: %v", addr, err)
		return nil, err
	}
	gServer := grpc.NewServer(serverOpts...)
	server := &RPCServer{addr: listener.Addr(), gServer: gServer, isTLS: isTLS}
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

// Ping handles ping request.
func (s *RPCServer) Ping(ctx context.Context, in *pb.PingRequest) (*pb.PingResponse, error) {
	util.FileLogger().Debugf("Received: %v", in.Data)
	return &pb.PingResponse{Data: in.Data}, nil
}

// ExecuteCommand executes a command on the server.
func (s *RPCServer) ExecuteCommand(
	req *pb.ExecuteCommandRequest,
	stream pb.NodeAgent_ExecuteCommandServer,
) error {
	var res *pb.ExecuteCommandResponse
	cmd := req.GetCommand()
	task := task.NewShellTask("RemoteCommand", cmd[0], cmd[1:])
	out, err := task.Process(stream.Context())
	if err == nil {
		res = &pb.ExecuteCommandResponse{
			Data: &pb.ExecuteCommandResponse_Output{
				Output: out,
			},
		}
	} else {
		util.FileLogger().Errorf("Error in running command: %s - %s", cmd, err.Error())
		res = &pb.ExecuteCommandResponse{
			Data: &pb.ExecuteCommandResponse_Error{
				Error: &pb.Error{
					Message: out,
				},
			},
		}
	}
	err = stream.Send(res)
	if err != nil {
		util.FileLogger().Errorf("Error in sending response - %s", err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	return nil
}

// UploadFile handles upload file to a specified file.
func (s *RPCServer) UploadFile(stream pb.NodeAgent_UploadFileServer) error {
	req, err := stream.Recv()
	if err != nil {
		util.FileLogger().Errorf("Error in receiving file info - %s", err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	fileInfo := req.GetFileInfo()
	filename := fileInfo.GetFilename()
	file, err := os.Create(filename)
	if err != nil {
		util.FileLogger().Errorf("Error in creating file %s - %s", filename, err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	defer file.Close()
	writer := bufio.NewWriter(file)
	defer writer.Flush()
	for {
		req, err = stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			util.FileLogger().Errorf("Error in reading from stream - %s", err.Error())
			return status.Error(codes.Internal, err.Error())
		}
		chunk := req.GetChunkData()
		size := len(chunk)
		util.FileLogger().Debugf("Received a chunk with size: %d", size)
		_, err = writer.Write(chunk)
		if err != nil {
			util.FileLogger().Errorf("Error in writing to file %s - %s", filename, err.Error())
			return status.Error(codes.Internal, err.Error())
		}
	}
	res := &pb.UploadFileResponse{}
	err = stream.SendAndClose(res)
	if err != nil {
		util.FileLogger().Errorf("Error in sending response - %s", err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	return nil
}

// DownloadFile downloads a specified file.
func (s *RPCServer) DownloadFile(
	in *pb.DownloadFileRequest,
	stream pb.NodeAgent_DownloadFileServer,
) error {
	filename := in.GetFilename()
	res := &pb.DownloadFileResponse{ChunkData: make([]byte, 1024)}
	file, err := os.Open(filename)
	if err != nil {
		util.FileLogger().Errorf("Error in opening file %s - %s", filename, err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	defer file.Close()
	for {
		n, err := file.Read(res.ChunkData)
		if err == io.EOF {
			break
		}
		if err != nil {
			util.FileLogger().Errorf("Error in reading file %s - %s", filename, err.Error())
			return status.Errorf(codes.Internal, err.Error())
		}
		res.ChunkData = res.ChunkData[:n]
		err = stream.Send(res)
		if err != nil {
			util.FileLogger().Errorf("Error in sending file %s - %s", filename, err.Error())
			return status.Errorf(codes.Internal, err.Error())
		}
	}
	return nil
}

/* End of gRPC methods. */
