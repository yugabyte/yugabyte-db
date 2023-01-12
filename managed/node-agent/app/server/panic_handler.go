package server

import (
	"context"
	"node-agent/util"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// UnaryPanicHandler returns the ServerOption to handle panic occurred in the unary interceptor
// or the downstream unary handler.
func UnaryPanicHandler(interceptor grpc.UnaryServerInterceptor) grpc.ServerOption {
	return grpc.UnaryInterceptor(func(
		ctx context.Context,
		req interface{},
		info *grpc.UnaryServerInfo,
		handler grpc.UnaryHandler,
	) (response interface{}, err error) {
		defer func() {
			if e := recover(); e != nil {
				util.FileLogger().Errorf("Panic occurred: %v", err)
				err = status.Errorf(codes.Internal, "Internal error occurred")
			}
		}()
		if interceptor == nil {
			response, err = handler(ctx, req)
		} else {
			response, err = interceptor(ctx, req, info, handler)
		}
		return
	})
}

// StreamPanicHandler returns the ServerOption to handle panic occurred in the stream interceptor
// or the downstream stream handler.
func StreamPanicHandler(interceptor grpc.StreamServerInterceptor) grpc.ServerOption {
	return grpc.StreamInterceptor(func(
		srv interface{},
		stream grpc.ServerStream,
		info *grpc.StreamServerInfo,
		handler grpc.StreamHandler,
	) (err error) {
		defer func() {
			if e := recover(); e != nil {
				util.FileLogger().Errorf("Panic occurred: %v", e)
				err = status.Errorf(codes.Internal, "Internal error occurred")
			}
		}()
		if interceptor == nil {
			err = handler(srv, stream)
		} else {
			err = interceptor(srv, stream, info, handler)
		}
		return
	})
}
