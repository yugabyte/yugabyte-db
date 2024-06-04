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
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
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
    Map<Pair<UUID, String>, List<Long>> universeToQueryId =
        ids.stream()
            .collect(
                Collectors.groupingBy(
                    qid -> ImmutablePair.of(qid.getUniverseId(), qid.getDbId()),
                    Collectors.mapping(PgStatStatementsQueryId::getQueryId, Collectors.toList())));
    return universeToQueryId.entrySet().stream()
        .map(
            entry ->
                new QPgStatStatementsQuery()
                    .id
                    .universeId
                    .eq(entry.getKey().getKey())
                    .id
                    .dbId
                    .eq(entry.getKey().getValue())
                    .id
                    .queryId
                    .in(entry.getValue())
                    .findList())
        .flatMap(List::stream)
        .toList();
  }

  public List<PgStatStatementsQuery> listByDatabaseId(UUID universeUuid, String dbId) {
    return new QPgStatStatementsQuery().id.universeId.eq(universeUuid).id.dbId.eq(dbId).findList();
  }

  public List<PgStatStatementsQuery> listByQueryId(UUID universeUuid, Long queryId) {
    return new QPgStatStatementsQuery()
        .id
        .universeId
        .eq(universeUuid)
        .id
        .queryId
        .eq(queryId)
        .findList();
  }

  public List<PgStatStatementsQuery> listByUniverseId(UUID universeUuid) {
    return new QPgStatStatementsQuery().id.universeId.eq(universeUuid).findList();
  }

  public List<PgStatStatementsQuery> listAll() {
    return new QPgStatStatementsQuery().findList();
  }

  @Override
  protected PgStatStatementsQuery prepareForSave(
      PgStatStatementsQuery entity, PgStatStatementsQuery before) {
    if (before == null || before.getLastActive() == null) {
      return entity;
    }
    if (entity.getLastActive() == null || entity.getLastActive().isBefore(before.getLastActive())) {
      // Always prefer max out of 2
      entity.setLastActive(before.getLastActive());
    }
    return super.prepareForSave(entity, before);
  }
}
