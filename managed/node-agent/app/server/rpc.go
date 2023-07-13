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
	"net/http"
	"node-agent/app/task"
	pb "node-agent/generated/service"
	"node-agent/metric"
	"node-agent/model"
	"node-agent/util"
	"os"
	"path/filepath"

	"node-agent/cmux"

	"golang.org/x/net/http2"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/status"
)

// RPCServer is the struct for gRPC server.
type RPCServer struct {
	listener     net.Listener
	gServer      *grpc.Server
	metricServer *http.Server
	isTLS        bool
	done         chan struct{}
}

// RPCServerConfig is the config for RPC server.
type RPCServerConfig struct {
	// Address is the server address.
	Address string
	// EnableTLS is to enable TLS for both RPC and metric servers.
	EnableTLS bool
	// EnableMetrics is to enable metrics.
	EnableMetrics bool
	// DisableMetricsTLS is to disable TLS for metrics when EnableTLS is to true.
	DisableMetricsTLS bool
}

// NewRPCServer creates an instance of gRPC server.
func NewRPCServer(
	ctx context.Context,
	serverConfig *RPCServerConfig,
) (*RPCServer, error) {
	serverOpts := []grpc.ServerOption{}
	unaryInterceptors := []grpc.UnaryServerInterceptor{UnaryPanicHandler()}
	streamInterceptors := []grpc.StreamServerInterceptor{StreamPanicHandler()}
	if serverConfig.EnableMetrics {
		unaryInterceptors = append(unaryInterceptors, UnaryMetricHandler())
		streamInterceptors = append(streamInterceptors, StreamMetricHandler())
	}
	var tlsConfig *tls.Config
	if serverConfig.EnableTLS {
		config, err := loadTLSConfig()
		if err != nil {
			util.FileLogger().Errorf(ctx, "Error in loading TLS credentials: %s", err)
			return nil, err
		}
		tlsConfig = config
	}
	listener, err := net.Listen("tcp", serverConfig.Address)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Failed to listen to %s: %v", serverConfig.Address, err)
		return nil, err
	}
	if serverConfig.EnableTLS {
		if serverConfig.DisableMetricsTLS {
			// Let gRPC handle TLS for RPC server.
			serverOpts = append(serverOpts, grpc.Creds(credentials.NewTLS(tlsConfig)))
		} else {
			// Create a new listener with TLS for both RPC and metrics server.
			listener = tls.NewListener(
				listener,
				&tls.Config{
					Certificates: tlsConfig.Certificates,
					NextProtos:   []string{http2.NextProtoTLS, "http/1.1"},
				},
			)
		}
	}
	if serverConfig.EnableTLS {
		authenticator := &Authenticator{util.CurrentConfig()}
		unaryInterceptors = append(unaryInterceptors, authenticator.UnaryInterceptor())
		streamInterceptors = append(streamInterceptors, authenticator.StreamInterceptor())
	}
	mux := cmux.New(listener)
	mListener := mux.Match(cmux.HTTP1())
	gListener := mux.Match(cmux.Any())
	serverOpts = append(serverOpts, grpc.ChainUnaryInterceptor(unaryInterceptors...))
	serverOpts = append(serverOpts, grpc.ChainStreamInterceptor(streamInterceptors...))
	gServer := grpc.NewServer(serverOpts...)
	server := &RPCServer{
		listener:     listener,
		gServer:      gServer,
		metricServer: &http.Server{},
		isTLS:        serverConfig.EnableTLS,
		done:         make(chan struct{}),
	}
	pb.RegisterNodeAgentServer(gServer, server)
	metric.GetInstance().PrepopulateMetrics(gServer)
	http.Handle("/metrics", metric.GetInstance().HTTPHandler())
	// Start metrics server.
	go func() {
		if err := server.metricServer.Serve(mListener); err != nil {
			util.FileLogger().Errorf(ctx, "Failed to start metrics server: %v", err)
		}
		select {
		case <-server.done:
			return
		}
		close(server.done)
	}()
	// Start RPC server.
	go func() {
		if err := gServer.Serve(gListener); err != nil {
			util.FileLogger().Errorf(ctx, "Failed to start RPC server: %v", err)
		}
		select {
		case <-server.done:
			return
		}
		close(server.done)
	}()
	// Start the root listener.
	go func() {
		if err := mux.Serve(); err != nil {
			util.FileLogger().Errorf(ctx, "Failed to start server: %v", err)
		}
		select {
		case <-server.done:
			return
		}
		close(server.done)
	}()
	return server, nil
}

func loadTLSConfig() (*tls.Config, error) {
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
	return tlsConfig, nil
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

// Addr returns the server address.
func (server *RPCServer) Addr() string {
	var addr string
	if server.listener != nil {
		addr = server.listener.Addr().String()
	}
	return addr
}

// Done returns a channel to check the status of the services.
func (server *RPCServer) Done() <-chan struct{} {
	return server.done
}

// Stop stops the services.
func (server *RPCServer) Stop() {
	if server.gServer != nil {
		server.gServer.GracefulStop()
	}
	server.gServer = nil
	if server.metricServer != nil {
		server.metricServer.Close()
	}
	server.metricServer = nil
	if server.listener != nil {
		server.listener.Close()
	}
	server.listener = nil
}

func (server *RPCServer) toPreflightCheckResponse(result any) (*pb.DescribeTaskResponse, error) {
	nodeConfigs := []*pb.NodeConfig{}
	err := util.ConvertType(result, &nodeConfigs)
	if err != nil {
		return nil, err
	}
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_PreflightCheckOutput{
			PreflightCheckOutput: &pb.PreflightCheckOutput{
				NodeConfigs: nodeConfigs,
			},
		},
	}, nil
}

/* Implementation of gRPC methods start here. */

// Ping handles ping request.
func (server *RPCServer) Ping(ctx context.Context, in *pb.PingRequest) (*pb.PingResponse, error) {
	util.FileLogger().Debugf(ctx, "Received ping")
	config := util.CurrentConfig()
	return &pb.PingResponse{
		ServerInfo: &pb.ServerInfo{
			Version:       config.String(util.PlatformVersionKey),
			RestartNeeded: config.Bool(util.NodeAgentRestartKey),
			Offloadable:   util.IsPexEnvAvailable(),
		},
	}, nil
}

// ExecuteCommand executes a command on the server.
func (server *RPCServer) ExecuteCommand(
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
func (server *RPCServer) SubmitTask(
	ctx context.Context,
	req *pb.SubmitTaskRequest,
) (*pb.SubmitTaskResponse, error) {
	res := &pb.SubmitTaskResponse{}
	taskID := req.GetTaskId()
	username := req.GetUser()
	cmdInput := req.GetCommandInput()
	if cmdInput != nil {
		cmd := cmdInput.GetCommand()
		shellTask := task.NewShellTaskWithUser("RemoteCommand", username, cmd[0], cmd[1:])
		err := task.GetTaskManager().Submit(ctx, taskID, shellTask, nil)
		if err != nil {
			return res, status.Error(codes.Internal, err.Error())
		}
		return res, nil
	}
	preflightCheckInput := req.GetPreflightCheckInput()
	if preflightCheckInput != nil {
		preflightCheckParam := &model.PreflightCheckParam{}
		err := util.ConvertType(preflightCheckInput, &preflightCheckParam)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Error in preflight input conversion - %s", err.Error())
			return res, status.Errorf(codes.InvalidArgument, err.Error())
		}
		preflightCheckHandler := task.NewPreflightCheckHandler(preflightCheckParam)
		err = task.GetTaskManager().
			Submit(ctx, taskID, preflightCheckHandler,
				util.RPCResponseConverter(server.toPreflightCheckResponse))
		if err != nil {
			util.FileLogger().Errorf(ctx, "Error in running preflight check - %s", err.Error())
			return res, status.Errorf(codes.Internal, err.Error())
		}
		return res, nil
	}
	return res, status.Error(codes.Unimplemented, "Unknown task")
}

// DescribeTask describes a submitted task.
// Client needs to retry on DeadlineExeeced error.
func (server *RPCServer) DescribeTask(
	req *pb.DescribeTaskRequest,
	stream pb.NodeAgent_DescribeTaskServer,
) error {
	ctx := stream.Context()
	taskID := req.GetTaskId()
	err := task.GetTaskManager().Subscribe(
		ctx,
		taskID,
		func(callbackData *task.TaskCallbackData) error {
			var res *pb.DescribeTaskResponse
			if callbackData.ExitCode == 0 {
				if callbackData.RPCResponse != nil {
					res = callbackData.RPCResponse
				} else {
					res = &pb.DescribeTaskResponse{
						State: callbackData.State.String(),
						Data: &pb.DescribeTaskResponse_Output{
							Output: callbackData.Info,
						},
					}
				}
			} else {
				res = &pb.DescribeTaskResponse{
					State: callbackData.State.String(),
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
func (server *RPCServer) AbortTask(
	ctx context.Context,
	req *pb.AbortTaskRequest,
) (*pb.AbortTaskResponse, error) {
	taskID, err := task.GetTaskManager().Abort(ctx, req.GetTaskId())
	if err != nil {
		return nil, status.Error(codes.Internal, err.Error())
	}
	return &pb.AbortTaskResponse{TaskId: taskID}, nil
}

// UploadFile handles upload file to a specified file.
func (server *RPCServer) UploadFile(stream pb.NodeAgent_UploadFileServer) error {
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
	userDetail, err := util.UserInfo(username)
	if err != nil {
		return status.Error(codes.Internal, err.Error())
	}
	util.FileLogger().Debugf(ctx, "Using user: %s, uid: %d, gid: %d",
		userDetail.User.Username, userDetail.UserID, userDetail.GroupID)
	if !filepath.IsAbs(filename) {
		filename = filepath.Join(userDetail.User.HomeDir, filename)
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
	if !userDetail.IsCurrent {
		err = file.Chown(int(userDetail.UserID), int(userDetail.GroupID))
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
func (server *RPCServer) DownloadFile(
	in *pb.DownloadFileRequest,
	stream pb.NodeAgent_DownloadFileServer,
) error {
	ctx := stream.Context()
	filename := in.GetFilename()
	res := &pb.DownloadFileResponse{ChunkData: make([]byte, 1024)}
	if !filepath.IsAbs(filename) {
		username := in.GetUser()
		userDetail, err := util.UserInfo(username)
		if err != nil {
			return status.Error(codes.Internal, err.Error())
		}
		util.FileLogger().Debugf(ctx, "Using user: %s, uid: %d, gid: %d",
			userDetail.User.Username, userDetail.UserID, userDetail.GroupID)
		filename = filepath.Join(userDetail.User.HomeDir, filename)
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

// Update updates the config file on upgrade.
func (server *RPCServer) Update(
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
		err = status.Errorf(codes.InvalidArgument, fmt.Sprintf("Unhandled state - %s", state))
	}
	res := &pb.UpdateResponse{Home: util.MustGetHomeDirectory()}
	return res, err
}

/* End of gRPC methods. */
