// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.PaUniverseInfo;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class PaUniversePagedApiQuery
    extends PagedQuery<PaUniverseApiFilter, PaUniverseInfo.SortBy> {}
