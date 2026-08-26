// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;

import api.v2.models.AuditLogConfig;
import api.v2.models.ClusterSpec;
import api.v2.models.QueryLogConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import org.junit.Test;

/**
 * Covers the edit-universe normalization: the spec -> userIntent mapping must coerce exportActive.
 */
public class UserIntentMapperTest {

  @Test
  public void toV1UserIntentNormalizesAuditExportActiveWithoutExporter() {
    // exportActive left true (the model default) with no exporter list configured.
    AuditLogConfig audit = new AuditLogConfig();
    audit.setExportActive(true);

    ClusterSpec spec = new ClusterSpec();
    spec.setAuditLogConfig(audit);

    UserIntent userIntent = UserIntentMapper.INSTANCE.toV1UserIntent(spec);

    assertNotNull(userIntent.auditLogConfig);
    assertFalse(
        "edit-universe must coerce audit exportActive off when no exporter is configured",
        userIntent.auditLogConfig.isExportActive());
  }

  @Test
  public void toV1UserIntentNormalizesQueryExportActiveWithoutExporter() {
    QueryLogConfig query = new QueryLogConfig();
    query.setExportActive(true);

    ClusterSpec spec = new ClusterSpec();
    spec.setQueryLogConfig(query);

    UserIntent userIntent = UserIntentMapper.INSTANCE.toV1UserIntent(spec);

    assertNotNull(userIntent.queryLogConfig);
    assertFalse(
        "edit-universe must coerce query exportActive off when no exporter is configured",
        userIntent.queryLogConfig.isExportActive());
  }
}
