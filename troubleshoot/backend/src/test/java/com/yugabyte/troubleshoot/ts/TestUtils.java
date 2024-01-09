package com.yugabyte.troubleshoot.ts;

import java.nio.charset.Charset;
import lombok.SneakyThrows;
import org.springframework.core.io.ClassPathResource;
import org.springframework.util.StreamUtils;

public class TestUtils {
  @SneakyThrows
  public static String readResource(String path) {
    return StreamUtils.copyToString(
        new ClassPathResource(path).getInputStream(), Charset.defaultCharset());
  }
}
