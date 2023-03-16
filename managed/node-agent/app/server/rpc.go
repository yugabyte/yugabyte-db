// Copyright (c) YugaByte, Inc.

package server

import (
	"bufio"
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net"
	"node-agent/app/executor"
	"node-agent/app/task"
	pb "node-agent/generated/service"
	"node-agent/model"
	"node-agent/util"
	"os"
	"os/user"
	"path/filepath"

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
			util.FileLogger().Errorf(ctx, "Error in loading TLS credentials: %s", err)
			return nil, err
		}
		authenticator := &Authenticator{util.CurrentConfig()}
		serverOpts = append(serverOpts, grpc.Creds(tlsCredentials))
		serverOpts = append(serverOpts, UnaryPanicHandler(authenticator.UnaryInterceptor()))
		serverOpts = append(serverOpts, StreamPanicHandler(authenticator.StreamInterceptor()))
	} else {
		serverOpts = append(serverOpts, UnaryPanicHandler(nil))
		serverOpts = append(serverOpts, StreamPanicHandler(nil))
	}
	listener, err := net.Listen("tcp", addr)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Failed to listen to %s: %v", addr, err)
		return nil, err
	}
	gServer := grpc.NewServer(serverOpts...)
	server := &RPCServer{
		addr:    listener.Addr(),
		gServer: gServer,
		isTLS:   isTLS,
	}
	pb.RegisterNodeAgentServer(gServer, server)
	go func() {
		if err := gServer.Serve(listener); err != nil {
			util.FileLogger().Errorf(ctx, "Failed to start RPC server: %v", err)
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

func removeFileIfPresent(filename string) error {
	stat, err := os.Stat(filename)
	if err == nil {
		if stat.IsDir() {
			err = errors.New("Path already exists as a directory")
		} else {
			err = os.Remove(filename)
		}
	} else if errors.Is(err, fs.ErrNotExist) {
		err = nil
	}
	return err
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
	util.FileLogger().Debugf(ctx, "Received ping")
	config := util.CurrentConfig()
	return &pb.PingResponse{
		ServerInfo: &pb.ServerInfo{
			Version:       config.String(util.PlatformVersionKey),
			RestartNeeded: config.Bool(util.NodeAgentRestartKey),
		},
	}, nil
}

// ExecuteCommand executes a command on the server.
func (s *RPCServer) ExecuteCommand(
	req *pb.ExecuteCommandRequest,
	stream pb.NodeAgent_ExecuteCommandServer,
) error {
	var res *pb.ExecuteCommandResponse
	ctx := stream.Context()
	cmd := req.GetCommand()
	username := req.GetUser()
	shellTask := task.NewShellTaskWithUser("RemoteCommand", username, cmd[0], cmd[1:])
	out, err := shellTask.Process(ctx)
	if err == nil {
		res = &pb.ExecuteCommandResponse{
			Data: &pb.ExecuteCommandResponse_Output{
				Output: out.Info.String(),
			},
		}
	} else {
		util.FileLogger().Errorf(ctx, "Error in running command: %s - %s", cmd, err.Error())
		res = &pb.ExecuteCommandResponse{
			Data: &pb.ExecuteCommandResponse_Error{
				Error: &pb.Error{
					Code:    int32(out.ExitStatus.Code),
					Message: out.ExitStatus.Error.String(),
				},
			},
		}
	}
	err = stream.Send(res)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in sending response - %s", err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	return nil
}

// SubmitTask submits an async task on the server.
func (s *RPCServer) SubmitTask(
	ctx context.Context,
	req *pb.SubmitTaskRequest,
) (*pb.SubmitTaskResponse, error) {
	cmd := req.GetCommand()
	taskID := req.GetTaskId()
	username := req.GetUser()
	shellTask := task.NewShellTaskWithUser("RemoteCommand", username, cmd[0], cmd[1:])
	err := task.GetTaskManager().Submit(ctx, taskID, shellTask)
	if err != nil {
		return nil, status.Error(codes.Internal, err.Error())
	}
	return &pb.SubmitTaskResponse{}, nil
}

// DescribeTask describes a submitted task.
// Client needs to retry on DeadlineExeeced error.
func (s *RPCServer) DescribeTask(
	req *pb.DescribeTaskRequest,
	stream pb.NodeAgent_DescribeTaskServer,
) error {
	ctx := stream.Context()
	taskID := req.GetTaskId()
	err := task.GetTaskManager().Subscribe(
		ctx,
		taskID,
		func(taskState executor.TaskState, callbackData *task.TaskCallbackData) error {
			var res *pb.DescribeTaskResponse
			if callbackData.ExitCode == 0 {
				res = &pb.DescribeTaskResponse{
					State: taskState.String(),
					Data: &pb.DescribeTaskResponse_Output{
						Output: callbackData.Info,
					},
				}
			} else {
				res = &pb.DescribeTaskResponse{
					State: taskState.String(),
					Data: &pb.DescribeTaskResponse_Error{
						Error: &pb.Error{
							Code:    int32(callbackData.ExitCode),
							Message: callbackData.Error,
						},
					},
				}
			}
			err := stream.Send(res)
			if err != nil {
				util.FileLogger().Errorf(ctx, "Error in sending response - %s", err.Error())
				return err
			}
			return nil
		})
	if err != nil && err != io.EOF {
		return status.Error(codes.Internal, err.Error())
	}
	return nil
}

// AbortTask aborts a running task.
func (s *RPCServer) AbortTask(
	ctx context.Context,
	req *pb.AbortTaskRequest,
) (*pb.AbortTaskResponse, error) {
	taskID, err := task.GetTaskManager().AbortTask(ctx, req.GetTaskId())
	if err != nil {
		return nil, status.Error(codes.Internal, err.Error())
	}
	return &pb.AbortTaskResponse{TaskId: taskID}, nil
}

// UploadFile handles upload file to a specified file.
func (s *RPCServer) UploadFile(stream pb.NodeAgent_UploadFileServer) error {
	ctx := stream.Context()
	req, err := stream.Recv()
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in receiving file info - %s", err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	fileInfo := req.GetFileInfo()
	filename := fileInfo.GetFilename()
	username := req.GetUser()
	chmod := req.GetChmod()
	userAcc, err := user.Current()
	if err != nil {
		return status.Error(codes.Internal, err.Error())
	}
	var uid, gid uint32
	var changeOwner = false
	if username != "" && userAcc.Username != username {
		userAcc, uid, gid, err = util.UserInfo(username)
		if err != nil {
			return status.Error(codes.Internal, err.Error())
		}
		util.FileLogger().Infof(ctx, "Using user: %s, uid: %d, gid: %d",
			userAcc.Username, uid, gid)
		changeOwner = true
	}
	if !filepath.IsAbs(filename) {
		filename = filepath.Join(userAcc.HomeDir, filename)
	}
	if chmod == 0 {
		// Do not care about file perm.
		// Set the default file mode in golang.
		chmod = 0666
	} else {
		// Get stat to remove the file if it exists because OpenFile does not change perm of
		// existing files. It simply truncates.
		err = removeFileIfPresent(filename)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Error in deleting existing file %s - %s", filename, err.Error())
			return status.Error(codes.Internal, err.Error())
		}
		util.FileLogger().Infof(ctx, "Setting file permission for %s to %o", filename, chmod)
	}
	file, err := os.OpenFile(filename, os.O_TRUNC|os.O_RDWR|os.O_CREATE, fs.FileMode(chmod))
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in creating file %s - %s", filename, err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	defer file.Close()
	if changeOwner {
		err = file.Chown(int(uid), int(gid))
		if err != nil {
			return status.Error(codes.Internal, err.Error())
		}
	}
	writer := bufio.NewWriter(file)
	defer writer.Flush()
	for {
		req, err = stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			util.FileLogger().Errorf(ctx, "Error in reading from stream - %s", err.Error())
			return status.Error(codes.Internal, err.Error())
		}
		chunk := req.GetChunkData()
		size := len(chunk)
		util.FileLogger().Debugf(ctx, "Received a chunk with size: %d", size)
		_, err = writer.Write(chunk)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Error in writing to file %s - %s", filename, err.Error())
			return status.Error(codes.Internal, err.Error())
		}
	}
	res := &pb.UploadFileResponse{}
	err = stream.SendAndClose(res)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in sending response - %s", err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	return nil
}

// DownloadFile downloads a specified file.
func (s *RPCServer) DownloadFile(
	in *pb.DownloadFileRequest,
	stream pb.NodeAgent_DownloadFileServer,
) error {
	ctx := stream.Context()
	filename := in.GetFilename()
	res := &pb.DownloadFileResponse{ChunkData: make([]byte, 1024)}
	if !filepath.IsAbs(filename) {
		username := in.GetUser()
		userAcc, err := user.Current()
		if err != nil {
			return status.Error(codes.Internal, err.Error())
		}
		var uid, gid uint32
		if username != "" && userAcc.Username != username {
			userAcc, uid, gid, err = util.UserInfo(username)
			if err != nil {
				return status.Error(codes.Internal, err.Error())
			}
			util.FileLogger().Infof(ctx, "Using user: %s, uid: %d, gid: %d",
				userAcc.Username, uid, gid)
		}
		filename = filepath.Join(userAcc.HomeDir, filename)
	}
	file, err := os.Open(filename)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in opening file %s - %s", filename, err.Error())
		return status.Error(codes.Internal, err.Error())
	}
	defer file.Close()
	for {
		n, err := file.Read(res.ChunkData)
		if err == io.EOF {
			break
		}
		if err != nil {
			util.FileLogger().Errorf(ctx, "Error in reading file %s - %s", filename, err.Error())
			return status.Errorf(codes.Internal, err.Error())
		}
		res.ChunkData = res.ChunkData[:n]
		err = stream.Send(res)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Error in sending file %s - %s", filename, err.Error())
			return status.Errorf(codes.Internal, err.Error())
		}
	}
	return nil
}

func (s *RPCServer) Update(
	ctx context.Context,
	in *pb.UpdateRequest,
) (*pb.UpdateResponse, error) {
	var err error
	config := util.CurrentConfig()
	state := in.GetState()
	switch state {
	case model.Upgrade.Name():
		// Start the upgrade process as all the files are available.
		err = HandleUpgradeState(ctx, config, in.GetUpgradeInfo())
	case model.Upgraded.Name():
		// Platform has confirmed that it has also rotated the cert and the key.
		err = HandleUpgradedState(ctx, config)
	default:
		err = fmt.Errorf("Unhandled state - %s", state)
	}
	res := &pb.UpdateResponse{Home: util.MustGetHomeDirectory()}
	return res, err
}

/* End of gRPC methods. */
