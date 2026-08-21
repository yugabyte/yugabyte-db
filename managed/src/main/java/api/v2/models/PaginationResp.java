package api.v2.models;

import com.fasterxml.jackson.annotation.*;
import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.fasterxml.jackson.databind.annotation.JsonNaming;
import io.ebean.PagedList;
import java.util.List;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.validation.*;
import javax.validation.constraints.*;
import lombok.AccessLevel;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.experimental.Accessors;

@Data
@EqualsAndHashCode
@Accessors(chain = true)
@JsonNaming(PropertyNamingStrategies.SnakeCaseStrategy.class)
public class PaginationResp<E> {

  @Getter(AccessLevel.NONE)
  private boolean hasNext;

  @Getter(AccessLevel.NONE)
  private boolean hasPrev;

  private int totalCount;
  private List<E> entities;

  public PaginationResp() {}

  public PaginationResp(PagedList<?> pagedList) {
    this.setHasNext(pagedList.hasNext())
        .setHasPrev(pagedList.hasPrev())
        .setTotalCount(pagedList.getTotalCount());
  }

  public <M> PaginationResp(PagedList<M> pagedList, Function<M, E> mapper) {
    this(pagedList);
    this.entities = pagedList.getList().stream().map(mapper).collect(Collectors.toList());
  }

  public boolean getHasNext() {
    return hasNext;
  }

  public boolean getHasPrev() {
    return hasPrev;
  }
}
