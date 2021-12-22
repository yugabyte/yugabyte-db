// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.filters.BackupApiFilter;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class BackupPagedApiQuery extends PagedQuery<BackupApiFilter, Backup.SortBy> {}
