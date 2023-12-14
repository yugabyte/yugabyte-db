package com.yugabyte.troubleshoot.ts.models;

public interface ModelWithId<K> {
  K getId();

  default boolean hasId() {
    return getId() != null;
  }

  void generateId();

  boolean delete();
}
