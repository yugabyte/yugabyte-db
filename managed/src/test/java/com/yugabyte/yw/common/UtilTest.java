package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.Customer;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;

import static org.junit.Assert.assertEquals;

@RunWith(MockitoJUnitRunner.class)
public class UtilTest extends FakeDBApplication {

    @Test
    public void testGetNodePrefixWithInvalidCustomerId() {
        try {
            Util.getNodePrefix(1L, "demo");
        } catch (Exception e) {
            assertEquals("Invalid Customer Id: 1", e.getMessage());
        }
    }

    @Test
    public void testGetNodePrefix() {
        Customer c = ModelFactory.testCustomer();
        String nodePrefix = Util.getNodePrefix(c.getCustomerId(), "demo");
        assertEquals("yb-tc-demo", nodePrefix);
    }

    @Test
    public void testGetKeysNotPresent() {
      Map<String, String> existing = ImmutableMap.of("Cust", "Test",
                                                     "Dept", "HR",
                                                     "Remove", "This",
                                                     "This", "Also");
      Map<String, String> newMap = ImmutableMap.of("Cust", "Test",
                                                   "Dept", "NewHR");
      assertEquals(Util.getKeysNotPresent(existing, newMap), "This,Remove");

      newMap = ImmutableMap.of("Cust", "Test",
                               "Dept", "NewHR",
                               "Remove", "ABCD",
                               "This", "BCDF",
                               "Just", "Coz");
      assertEquals(Util.getKeysNotPresent(existing, newMap), "");

      assertEquals(existing.size(), 4);
      assertEquals(newMap.size(), 5);
    }

    // TODO: Add tests for other functions
}
