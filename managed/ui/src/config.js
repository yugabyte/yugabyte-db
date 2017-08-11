// Copyright (c) YugaByte, Inc.

export const ROOT_URL = process.env.NODE_ENV === 'development' ? 'http://localhost:9000/api' : '/api';
export const MAP_SERVER_URL = process.env.NODE_ENV === 'development' ? 'https://no-such-url' : '/map';
