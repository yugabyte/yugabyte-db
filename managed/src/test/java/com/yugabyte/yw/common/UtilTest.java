package com.yugabyte.yw.common;

import com.yugabyte.yw.models.Customer;
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

    // TODO: Add tests for other functions
}
