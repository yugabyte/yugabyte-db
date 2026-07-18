package com.yugabyte.troubleshoot.ts.models;

import java.io.Serializable;
import java.util.UUID;
import javax.persistence.Embeddable;
import lombok.AllArgsConstructor;
import lombok.Data;

@Embeddable
@Data
@AllArgsConstructor
public class RuntimeConfigEntryKey implements Serializable {
  private final UUID scopeUUID;
  private final String path;
}
