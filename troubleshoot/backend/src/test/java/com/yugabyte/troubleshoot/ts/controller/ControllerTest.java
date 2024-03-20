package com.yugabyte.troubleshoot.ts.controller;

import com.yugabyte.troubleshoot.ts.service.ServiceTest;
import io.zonky.test.db.AutoConfigureEmbeddedDatabase.RefreshMode;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import org.springframework.boot.test.autoconfigure.web.servlet.AutoConfigureMockMvc;
import org.springframework.core.annotation.AliasFor;

@Target({ElementType.TYPE})
@Retention(RetentionPolicy.RUNTIME)
@ServiceTest
@AutoConfigureMockMvc
public @interface ControllerTest {
  @AliasFor(annotation = ServiceTest.class, attribute = "refresh")
  RefreshMode refresh() default RefreshMode.BEFORE_EACH_TEST_METHOD;
}
