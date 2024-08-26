// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.backuprestore;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnore;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;

@Data
public class KeyspaceTables {
  @ApiModelProperty(value = "Tables")
  private Set<String> tableNames = new HashSet<>();

  @ApiModelProperty(value = "Keyspace")
  private String keyspace;

  @JsonCreator
  public KeyspaceTables(Set<String> tableNames, String keyspace) {
    if (CollectionUtils.isNotEmpty(tableNames)) {
      this.tableNames = new HashSet<>(tableNames);
    }
    this.keyspace = keyspace;
  }

  @JsonIgnore
  public Map.Entry<String, Set<String>> getKeyspaceTablesMapEntry() {
    KeyspaceTables kT = new KeyspaceTables(this.getTableNames(), this.keyspace);
    return new Map.Entry<String, Set<String>>() {
      @Override
      public String getKey() {
        return kT.keyspace;
      }

      @Override
      public Set<String> getValue() {
        return kT.tableNames;
      }

      @Override
      public Set<String> setValue(Set<String> newTableNames) {
        kT.tableNames = newTableNames;
        return getValue();
      }
    };
  }
}
