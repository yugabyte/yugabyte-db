// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import api.v2.models.AuditLogConfig;
import api.v2.models.ClusterNetworkingSpec;
import api.v2.models.ClusterSpec;
import api.v2.models.ManagedLoadBalancerSpec;
import api.v2.models.QueryLogConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import org.junit.Test;

/**
 * Covers the spec -> userIntent mapping: the edit-universe normalization must coerce exportActive,
 * and a managed load balancer request must map without enable_lb.
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
  public void toV1UserIntentMapsManagedLoadBalancerWithoutEnableLb() {
    ClusterSpec spec = new ClusterSpec();
    spec.setNetworkingSpec(
        new ClusterNetworkingSpec()
            .managedLoadBalancer(
                new ManagedLoadBalancerSpec().enablePrivate(true).enablePublic(true)));

    UserIntent userIntent = UserIntentMapper.INSTANCE.toV1UserIntent(spec);

    assertTrue(userIntent.isManagedLoadBalancerEnabled());
    assertTrue(userIntent.getManagedLoadBalancer().isEnablePrivate());
    assertTrue(userIntent.getManagedLoadBalancer().isEnablePublic());
    assertFalse(userIntent.enableLB);
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
