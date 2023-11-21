// Copyright (c) YugaByte, Inc.

package server

import (
	"context"
	"node-agent/app/session"
	"node-agent/util"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/status"
)

// Authenticator is the server auth handler.
type Authenticator struct {
	config *util.Config
}

func (authenticator *Authenticator) authorize(ctx context.Context, method string) error {
	md, ok := metadata.FromIncomingContext(ctx)
	if !ok {
		return status.Errorf(codes.Unauthenticated, "Metadata is not provided")
	}

	values := md["authorization"]
	if len(values) == 0 {
		return status.Errorf(codes.Unauthenticated, "Authorization token is not provided")
	}
	accessToken := values[0]
	// This validation enhances the performance of ansible calls.
	claims, err := util.ExtractClaims(ctx, authenticator.config, accessToken)
	if err == nil {
		if sessionKey, ok := util.StringFromClaims(claims, util.JwtClaimsSessionKey); ok {
			entry := session.GetCacheInstance().Get(ctx, sessionKey)
			if entry != nil {
				return nil
			}
		}
	}
	// Default to this validation if session validation does not work.
	claims, err = util.VerifyJWT(ctx, authenticator.config, accessToken)
	if err != nil {
		return status.Errorf(codes.Unauthenticated, "Authorization token is invalid: %v", err)
	}
	if sessionKey, ok := util.StringFromClaims(claims, util.JwtClaimsSessionKey); ok {
		if expiry, ok := util.IntFromClaims(claims, util.JwtClaimsExpiryKey); ok {
			session.GetCacheInstance().
				Put(ctx, &session.SessionEntry{ID: sessionKey, Info: claims, Expiry: time.Unix(expiry, 0)})
		}
	}
	return nil
}

// UnaryInterceptor returns the unary server interceptor to intercept the request to authorize.
func (authenticator *Authenticator) UnaryInterceptor() grpc.UnaryServerInterceptor {
	return grpc.UnaryServerInterceptor(func(
		ctx context.Context,
		req interface{},
		info *grpc.UnaryServerInfo,
		handler grpc.UnaryHandler,
	) (interface{}, error) {
		err := authenticator.authorize(ctx, info.FullMethod)
		if err != nil {
			return nil, err
		}
		return handler(ctx, req)
	})
}

// StreamInterceptor returns the stream server interceptor to intercept the request to authorize.
func (authenticator *Authenticator) StreamInterceptor() grpc.StreamServerInterceptor {
	return grpc.StreamServerInterceptor(func(srv interface{},
		stream grpc.ServerStream,
		info *grpc.StreamServerInfo,
		handler grpc.StreamHandler,
	) error {
		err := authenticator.authorize(stream.Context(), info.FullMethod)
		if err != nil {
			return err
		}
		return handler(srv, stream)
	})
}
