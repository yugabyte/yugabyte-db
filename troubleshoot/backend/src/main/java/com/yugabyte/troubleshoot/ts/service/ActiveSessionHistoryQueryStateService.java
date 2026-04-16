package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.ActiveSessionHistoryQueryState;
import com.yugabyte.troubleshoot.ts.models.ActiveSessionHistoryQueryStateId;
import com.yugabyte.troubleshoot.ts.models.query.QActiveSessionHistoryQueryState;
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
public class ActiveSessionHistoryQueryStateService
    extends TSMetadataServiceBase<
        ActiveSessionHistoryQueryState, ActiveSessionHistoryQueryStateId> {

  public ActiveSessionHistoryQueryStateService(Database database, BeanValidator validator) {
    super(database, validator);
  }

  @Override
  public String entityType() {
    return "Active Session History Query State";
  }

  @Override
  public List<ActiveSessionHistoryQueryState> listByIds(
      Collection<ActiveSessionHistoryQueryStateId> ids) {
    Map<UUID, List<String>> universeToNodeName =
        ids.stream()
            .collect(
                Collectors.groupingBy(
                    ActiveSessionHistoryQueryStateId::getUniverseId,
                    Collectors.mapping(
                        ActiveSessionHistoryQueryStateId::getNodeName, Collectors.toList())));
    return universeToNodeName.entrySet().stream()
        .map(
            entry ->
                new QActiveSessionHistoryQueryState()
                    .id
                    .universeId
                    .eq(entry.getKey())
                    .id
                    .nodeName
                    .in(entry.getValue())
                    .findList())
        .flatMap(List::stream)
        .toList();
  }

  public List<ActiveSessionHistoryQueryState> listByUniverseId(UUID universeUuid) {
    return new QActiveSessionHistoryQueryState().id.universeId.eq(universeUuid).findList();
  }

  public List<ActiveSessionHistoryQueryState> listAll() {
    return new QActiveSessionHistoryQueryState().findList();
  }
}
