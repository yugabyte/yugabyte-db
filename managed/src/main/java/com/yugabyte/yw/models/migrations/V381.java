package com.yugabyte.yw.models.migrations;

import com.yugabyte.yw.models.RuntimeConfigEntryKey;
import io.ebean.Finder;
import io.ebean.Model;
import jakarta.persistence.EmbeddedId;
import jakarta.persistence.Entity;
import java.nio.charset.StandardCharsets;
import java.util.UUID;

public class V381 {

  @Entity
  public static class RuntimeConfigEntry extends Model {
    public static final Finder<UUID, RuntimeConfigEntry> find =
        new Finder<UUID, RuntimeConfigEntry>(RuntimeConfigEntry.class) {};

    private byte[] value;
    @EmbeddedId private final RuntimeConfigEntryKey idKey;

    public RuntimeConfigEntry(UUID scopedConfigId, String path, String value) {
      this.idKey = new RuntimeConfigEntryKey(scopedConfigId, path);
      this.setValue(value);
    }

    public void setValue(String value) {
      this.value = value.getBytes(StandardCharsets.UTF_8);
    }

    public String getValue() {
      return new String(this.value, StandardCharsets.UTF_8);
    }
  }
}
