package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.PgStatStatementsQuery;
import com.yugabyte.troubleshoot.ts.models.PgStatStatementsQueryId;
import com.yugabyte.troubleshoot.ts.models.query.QPgStatStatementsQuery;
import io.ebean.Database;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class PgStatStatementsQueryService
    extends TSMetadataServiceBase<PgStatStatementsQuery, PgStatStatementsQueryId> {

  public PgStatStatementsQueryService(Database database, BeanValidator validator) {
    super(database, validator);
  }

  @Override
  public String entityType() {
    return "PgStatStatements Query Metadata";
  }

  @Override
  public List<PgStatStatementsQuery> listByIds(Collection<PgStatStatementsQueryId> ids) {
    Map<UUID, List<Long>> universeToQueryId =
        ids.stream()
            .collect(
                Collectors.groupingBy(
                    PgStatStatementsQueryId::getUniverseId,
                    Collectors.mapping(PgStatStatementsQueryId::getQueryId, Collectors.toList())));
    return universeToQueryId.entrySet().stream()
        .map(
            entry ->
                new QPgStatStatementsQuery()
                    .id
                    .universeId
                    .eq(entry.getKey())
                    .id
                    .queryId
                    .in(entry.getValue())
                    .findList())
        .flatMap(List::stream)
        .toList();
  }

  public List<PgStatStatementsQuery> listByUniverseId(UUID universeUuid) {
    return new QPgStatStatementsQuery().id.universeId.eq(universeUuid).findList();
  }

  public List<PgStatStatementsQuery> listAll() {
    return new QPgStatStatementsQuery().findList();
  }
}
