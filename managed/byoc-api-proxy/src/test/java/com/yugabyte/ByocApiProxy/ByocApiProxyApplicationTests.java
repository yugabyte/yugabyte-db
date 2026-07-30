package com.yugabyte.ByocApiProxy;

import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.TestPropertySource;

@SpringBootTest
@TestPropertySource(properties = "spring.task.scheduling.enabled=false")
class ByocApiProxyApplicationTests {

  @Test
  void contextLoads() {}
}
