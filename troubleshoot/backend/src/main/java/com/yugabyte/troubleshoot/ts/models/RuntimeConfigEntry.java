package com.yugabyte.troubleshoot.ts.models;

import io.ebean.Model;
import java.util.*;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
@Entity
public class RuntimeConfigEntry extends Model implements ModelWithId<RuntimeConfigEntryKey> {

  @EmbeddedId private final RuntimeConfigEntryKey key;

  private String value;

  public RuntimeConfigEntry(UUID scopeUuid, String path, String value) {
    this.key = new RuntimeConfigEntryKey(scopeUuid, path);
    this.setValue(value);
  }

  public String getPath() {
    return key.getPath();
  }

  public UUID getScopeUUID() {
    return key.getScopeUUID();
  }

  @Override
  public RuntimeConfigEntryKey getId() {
    return key;
  }
}
