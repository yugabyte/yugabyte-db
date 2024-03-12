/*
 * Copyright 2007 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.yugabyte.troubleshoot.ts.cvs;

import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import net.sf.jsefa.common.converter.ConversionException;
import net.sf.jsefa.common.converter.SimpleTypeConverter;
import net.sf.jsefa.common.converter.SimpleTypeConverterConfiguration;
import org.apache.commons.lang3.StringUtils;

public class InstantConverter implements SimpleTypeConverter {
  private static final String DEFAULT_FORMAT = "yyyy-MM-dd'T'HH:mm:ss'Z'";

  private final DateTimeFormatter formatter;

  public static InstantConverter create(SimpleTypeConverterConfiguration configuration) {
    return new InstantConverter(configuration);
  }

  protected InstantConverter(SimpleTypeConverterConfiguration configuration) {
    String format = getFormat(configuration);
    try {
      this.formatter = DateTimeFormatter.ofPattern(format);
    } catch (Exception e) {
      throw new ConversionException(
          "Could not create a " + this.getClass().getName() + "  with format " + format, e);
    }
  }

  public final synchronized Instant fromString(String value) {
    if (StringUtils.isBlank(value)) {
      return null;
    }
    LocalDateTime ldt = LocalDateTime.parse(value, formatter);
    return ldt.toInstant(ZoneOffset.UTC);
  }

  public final synchronized String toString(Object value) {
    if (value == null) {
      return null;
    }
    return this.formatter.format((Instant) value);
  }

  protected String getDefaultFormat() {
    return InstantConverter.DEFAULT_FORMAT;
  }

  private String getFormat(SimpleTypeConverterConfiguration configuration) {
    if (configuration.getFormat() == null) {
      return getDefaultFormat();
    }
    if (configuration.getFormat().length != 1) {
      throw new ConversionException("The format for a InstantConverter must be a single String");
    }
    return configuration.getFormat()[0];
  }
}
