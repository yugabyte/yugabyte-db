package integration;

// Copyright (c) Yugabyte, Inc.

import org.junit.*;
import static play.test.Helpers.*;
import static org.junit.Assert.*;

public class HomeControllerIntegrationTest {
    @Test
    public void test() {
        running(testServer(3333, fakeApplication(inMemoryDatabase())), HTMLUNIT, browser -> {
            browser.goTo("http://localhost:3333");
            assertTrue(browser.pageSource().contains("Yugabyte Middleware APIs."));
        });
    }

}
