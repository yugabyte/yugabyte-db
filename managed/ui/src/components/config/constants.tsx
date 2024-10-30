// Copyright (c) YugaByte, Inc.

export const ACCEPTABLE_CHARS = /^[a-zA-Z0-9_-]+$/;

export const VPC_ID_REGEX = /^[a-z]([a-z0-9-]{0,61}[a-z0-9])?$/;

export const RG_REGEX = /^[\w()-.][\w()-.]*[\w()-]$|^[\w()-]$/;

export const UUID_REGEX = /^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/;
