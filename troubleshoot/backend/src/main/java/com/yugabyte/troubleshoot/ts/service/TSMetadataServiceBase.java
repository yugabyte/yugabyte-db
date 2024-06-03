package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.ModelWithId;
import io.ebean.Database;
import io.ebean.annotation.Transactional;
import java.util.*;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public abstract class TSMetadataServiceBase<T extends ModelWithId<K>, K> {

  protected final Database database;
  protected final BeanValidator beanValidator;

  public TSMetadataServiceBase(Database database, BeanValidator beanValidator) {
    this.database = database;
    this.beanValidator = beanValidator;
  }

  @Transactional
  public List<T> save(List<T> entities) {
    if (CollectionUtils.isEmpty(entities)) {
      return entities;
    }

    List<T> beforeEntities = Collections.emptyList();
    Set<K> ids =
        entities.stream()
            .filter(ModelWithId::hasId)
            .map(ModelWithId::getId)
            .collect(Collectors.toSet());
    if (!ids.isEmpty()) {
      beforeEntities = listByIds(ids);
    }
    Map<K, T> beforeMap =
        beforeEntities.stream().collect(Collectors.toMap(ModelWithId::getId, Function.identity()));

    Map<EntityOperation, List<T>> toCreateAndUpdate =
        entities.stream()
            .map(entity -> prepareForSave(entity, beforeMap.get(entity.getId())))
            .filter(entity -> filterForSave(entity, beforeMap.get(entity.getId())))
            .peek(entity -> validate(entity, beforeMap.get(entity.getId())))
            .collect(
                Collectors.groupingBy(
                    entity ->
                        !beforeMap.containsKey(entity.getId())
                            ? EntityOperation.CREATE
                            : EntityOperation.UPDATE));

    List<T> toCreate =
        toCreateAndUpdate.getOrDefault(EntityOperation.CREATE, Collections.emptyList());
    if (!toCreate.isEmpty()) {
      toCreate.forEach(ModelWithId::generateId);
      database.saveAll(toCreate);
    }

    List<T> toUpdate =
        toCreateAndUpdate.getOrDefault(EntityOperation.UPDATE, Collections.emptyList());
    if (!toUpdate.isEmpty()) {
      database.updateAll(toUpdate);
    }

    log.trace("{} {}s saved", toCreate.size() + toUpdate.size(), entityType());
    return entities;
  }

  public abstract String entityType();

  public abstract List<T> listByIds(Collection<K> ids);

  @Transactional
  public T save(T entity) {
    return save(Collections.singletonList(entity)).get(0);
  }

  public T get(K id) {
    if (id == null) {
      throw new IllegalArgumentException("Can't get " + entityType() + " by null id");
    }
    return listByIds(Collections.singletonList(id)).stream().findFirst().orElse(null);
  }

  public T getOrThrow(K id) {
    T result = get(id);
    if (result == null) {
      beanValidator
          .error()
          .forField("id", "can't find " + entityType() + " '" + id + "'")
          .throwError();
    }
    return result;
  }

  @Transactional
  public void delete(K id) {
    T entity = get(id);
    if (entity == null) {
      log.warn("{} {} is already deleted", entityType(), id);
      return;
    }
    entity.delete();

    log.trace("{} {} deleted", entityType(), id);
  }

  protected T prepareForSave(T entity, T before) {
    return entity;
  }

  protected boolean filterForSave(T entity, T before) {
    if (before == null) {
      return true;
    }
    return !entity.equals(before);
  }

  protected void validate(T entity, T before) {
    beanValidator.validate(entity);
  }

  enum EntityOperation {
    CREATE,
    UPDATE,
    DELETE
  }
}
