// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import api.v2.models.PaginationResp;
import api.v2.models.PaginationSpec;
import api.v2.utils.NormalizedPaginationSpec;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.ExpressionList;
import io.ebean.PagedList;
import java.util.List;
import java.util.function.Function;
import java.util.stream.Collectors;

// Keep bounds in sync with PaginationSpec.yaml
public final class HandlerPagingSupport {

  static final int DEFAULT_PAGE_LIMIT = 10;
  static final int MAX_PAGE_LIMIT = 500;

  private HandlerPagingSupport() {}

  static int normalizedOffset(Integer offset) {
    return offset != null ? offset : 0;
  }

  static NormalizedPaginationSpec normalize(PaginationSpec spec) {
    validatePagination(spec);
    return new NormalizedPaginationSpec(
        HandlerPagingSupport.normalizedOffset(spec.getOffset()),
        HandlerPagingSupport.normalizedLimit(spec.getLimit()),
        sqlSortOrder(spec.getDirection()));
  }

  static int normalizedLimit(Integer limit) {
    return limit != null ? limit : DEFAULT_PAGE_LIMIT;
  }

  public static <T> PagedList<T> getPagedList(
      ExpressionList<T> list, NormalizedPaginationSpec spec, String orderBy) {
    return list.orderBy(orderBy)
        .setFirstRow(spec.offset())
        .setMaxRows(spec.limit())
        .findPagedList();
  }

  static void validatePagination(PaginationSpec spec) {
    int o = normalizedOffset(spec.getOffset());
    int l = normalizedLimit(spec.getLimit());
    if (o < 0) {
      throw new PlatformServiceException(BAD_REQUEST, "offset must be >= 0");
    }
    if (l < 1 || l > MAX_PAGE_LIMIT) {
      throw new PlatformServiceException(
          BAD_REQUEST, "limit must be between 1 and " + MAX_PAGE_LIMIT);
    }
  }

  /** Lowercase SQL {@code asc} / {@code desc} for {@code ORDER BY} clauses. */
  static String sqlSortOrder(PaginationSpec.DirectionEnum directionEnum) {
    return directionEnum == PaginationSpec.DirectionEnum.DESC ? "desc" : "asc";
  }

  static <M, I> List<I> mapPage(List<M> page, Function<M, I> mapper) {
    return page.stream().map(mapper).collect(Collectors.toList());
  }

  static <M, I> List<I> mapPage(PagedList<M> page, Function<M, I> mapper) {
    return mapPage(page.getList(), mapper);
  }

  static <M, I, R extends PaginationResp<I>> R pagedResponse(
      R resp, PagedList<M> page, Function<M, I> mapper) {
    resp.setHasNext(page.hasNext())
        .setHasPrev(page.hasPrev())
        .setTotalCount(page.getTotalCount())
        .setEntities(mapPage(page, mapper));
    return resp;
  }
}
