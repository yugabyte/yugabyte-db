package org.yb.cql;

import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestBindCollectionWithSubscriptedColumn extends TestBindVariable {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ycql_bind_collection_assignment_using_column_name", "true");
    return flags;
  }

  @Test
  @Override
  public void testCollectionBindUpdates() throws Exception {
    testCollectionBindUpdates(BindCollAssignmentByColName.ON);
  }
}
