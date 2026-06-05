// Copyright (c) YugabyteDB, Inc.

package api.v2.utils;

public record NormalizedPaginationSpec(int offset, int limit, String order) {}
