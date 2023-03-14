package server

import (
	"context"
	"node-agent/util"
	"runtime/debug"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/status"
)

type serverStream struct {
	grpc.ServerStream
	ctx context.Context
}

// Context returns the overridden context.
func (ss *serverStream) Context() context.Context {
	return ss.ctx
}

func withCorrelationID(srvCtx context.Context) context.Context {
	corrId := ""
	md, ok := metadata.FromIncomingContext(srvCtx)
	if ok {
		values := md[util.RequestIdHeader]
		if len(values) > 0 {
			corrId = values[0]
		}
	}
	if corrId == "" {
		corrId = util.NewUUID().String()
	}
	return util.WithCorrelationID(srvCtx, util.NewUUID().String())
}

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
				util.FileLogger().Errorf(ctx, "Panic occurred: %v", string(debug.Stack()))
				err = status.Errorf(codes.Internal, "Internal error occurred")
			}
		}()
		ctx = withCorrelationID(ctx)
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
		ctx := stream.Context()
		defer func() {
			if e := recover(); e != nil {
				util.FileLogger().Errorf(ctx, "Panic occurred: %v", string(debug.Stack()))
				err = status.Errorf(codes.Internal, "Internal error occurred")
			}
		}()
		ctx = withCorrelationID(ctx)
		stream = &serverStream{stream, ctx}
		if interceptor == nil {
			err = handler(srv, stream)
		} else {
			err = interceptor(srv, stream, info, handler)
		}
		return
	})
}
