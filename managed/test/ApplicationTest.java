// Copyright (c) Yugabyte, Inc.

import org.junit.*;
import play.twirl.api.Content;
import static org.junit.Assert.*;


public class ApplicationTest {
    @Test
    public void renderTemplate() {
        Content html = views.html.index.render("Your new application is ready.");
        assertEquals("text/html", html.contentType());
        assertTrue(html.body().contains("Your new application is ready."));
    }


}
