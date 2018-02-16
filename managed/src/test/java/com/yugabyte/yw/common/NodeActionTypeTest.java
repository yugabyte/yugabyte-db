// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.CustomerTask;
import org.junit.Test;

import static com.yugabyte.yw.models.helpers.TaskType.DeleteNodeFromUniverse;
import static com.yugabyte.yw.models.helpers.TaskType.StartNodeInUniverse;
import static com.yugabyte.yw.models.helpers.TaskType.StopNodeInUniverse;
import static org.junit.Assert.assertEquals;


public class NodeActionTypeTest {

    @Test
    public void testToString() {
        assertEquals("Starting", NodeActionType.START.toString(false));
        assertEquals("Started", NodeActionType.START.toString(true));
        assertEquals("Stopping", NodeActionType.STOP.toString(false));
        assertEquals("Stopped", NodeActionType.STOP.toString(true));
        assertEquals("Deleting", NodeActionType.DELETE.toString(false));
        assertEquals("Deleted", NodeActionType.DELETE.toString(true));
    }

    @Test
    public void testGetCommissionerTask() {
        assertEquals(StartNodeInUniverse, NodeActionType.START.getCommissionerTask());
        assertEquals(StopNodeInUniverse, NodeActionType.STOP.getCommissionerTask());
        assertEquals(DeleteNodeFromUniverse, NodeActionType.DELETE.getCommissionerTask());
    }

    @Test
    public void testGetCustomerTask() {
        assertEquals(CustomerTask.TaskType.Start, NodeActionType.START.getCustomerTask());
        assertEquals(CustomerTask.TaskType.Stop, NodeActionType.STOP.getCustomerTask());
        assertEquals(CustomerTask.TaskType.Delete, NodeActionType.DELETE.getCustomerTask());
    }
}
