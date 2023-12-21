package com.yugabyte.troubleshoot.ts.service;

import io.zonky.test.db.AutoConfigureEmbeddedDatabase;
import io.zonky.test.db.AutoConfigureEmbeddedDatabase.RefreshMode;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import org.flywaydb.test.annotation.FlywayTest;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.core.annotation.AliasFor;

@Target({ElementType.TYPE})
@Retention(RetentionPolicy.RUNTIME)
@SpringBootTest
@FlywayTest
@AutoConfigureEmbeddedDatabase(type = AutoConfigureEmbeddedDatabase.DatabaseType.POSTGRES)
public @interface ServiceTest {
  @AliasFor(annotation = AutoConfigureEmbeddedDatabase.class, attribute = "refresh")
  RefreshMode refresh() default RefreshMode.BEFORE_EACH_TEST_METHOD;
}
