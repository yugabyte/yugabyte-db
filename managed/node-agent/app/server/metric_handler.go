// Copyright (c) YugaByte, Inc.

package server

import (
	"context"
	"strings"
	"time"

	"node-agent/metric"

	"google.golang.org/grpc"
	"google.golang.org/grpc/status"
)

func serviceMethodNames(fullMethodName string) (string, string) {
	fullMethodName = strings.Trim(fullMethodName, "/")
	if i := strings.Index(fullMethodName, "/"); i >= 0 {
		return fullMethodName[:i], fullMethodName[i+1:]
	}
	// service name, method name.
	return fullMethodName, fullMethodName
}

// UnaryMetricHandler returns the unary server interceptor to intercept the request to monitor.
func UnaryMetricHandler() grpc.UnaryServerInterceptor {
	return grpc.UnaryServerInterceptor(func(
		ctx context.Context,
		req interface{},
		info *grpc.UnaryServerInfo,
		handler grpc.UnaryHandler,
	) (interface{}, error) {
		sName, mName := serviceMethodNames(info.FullMethod)
		startTime := time.Now()
		res, err := handler(ctx, req)
		st := status.Convert(err)
		metric.GetInstance().PublishServerMethodStats(
			time.Since(startTime),
			sName,
			mName,
			st.Code().String(),
		)
		return res, err
	})
}

// StreamMetricHandler returns the stream server interceptor to intercept the request to monitor.
func StreamMetricHandler() grpc.StreamServerInterceptor {
	return grpc.StreamServerInterceptor(func(srv interface{},
		stream grpc.ServerStream,
		info *grpc.StreamServerInfo,
		handler grpc.StreamHandler,
	) error {
		sName, mName := serviceMethodNames(info.FullMethod)
		startTime := time.Now()
		err := handler(srv, stream)
		st := status.Convert(err)
		metric.GetInstance().PublishServerMethodStats(
			time.Since(startTime),
			sName,
			mName,
			st.Code().String(),
		)
		return err
	})
}
