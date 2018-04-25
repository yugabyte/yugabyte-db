// Copyright (c) YugaByte, Inc.

export const ROOT_URL = process.env.REACT_APP_YUGAWARE_API_URL ||
  (process.env.NODE_ENV === 'development' ? 'http://localhost:9000/api' : '/api');

export const MAP_SERVER_URL = process.env.NODE_ENV === 'development'
  ? 'https://no-such-url'
  : '/map';

export const IN_DEVELOPMENT_MODE = process.env.NODE_ENV === 'development';

// TODO : probably fetch the provider metadata (name and code from backend)
export const PROVIDER_TYPES = [
  { code: "aws", name: "Amazon", label: "Amazon Web Services" },
  { code: "docker", name: "Docker Localhost", label: "Docker" },
  { code: "gcp", name: "Google", label: "Google Cloud" },
  { code: "onprem", name: "On Premises", label: "On-Premises Datacenter"}
];
