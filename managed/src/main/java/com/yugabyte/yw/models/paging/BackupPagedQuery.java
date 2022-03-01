// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.filters.BackupFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class BackupPagedQuery extends PagedQuery<BackupFilter, Backup.SortBy> {}
