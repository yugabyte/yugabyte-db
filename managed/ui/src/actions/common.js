// Copyright (c) YugaByte, Inc.
import { ROOT_URL } from '../config';

// TODO : probably fetch the provider metadata (name and code from backend)
export const PROVIDER_TYPES = [
  { code: "aws", name: "Amazon" },
  { code: "docker", name: "Docker Localhost" },
  { code: "gcp", name: "Google" },
  { code: "onprem", name: "On Premises"}
];

export function getProviderEndpoint(providerUUID) {
  const customerUUID = localStorage.getItem("customer_id");
  return `${ROOT_URL}/customers/${customerUUID}/providers/${providerUUID}`;
}

export function getCustomerEndpoint() {
  const customerUUID = localStorage.getItem("customer_id");
  return `${ROOT_URL}/customers/${customerUUID}`;
}
