// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;

public class DateTimeMapper {

  public OffsetDateTime toOffsetDateTime(Date date) {
    if (date == null) {
      return null;
    }
    return date.toInstant().atOffset(ZoneOffset.UTC);
  }

  public Date toDate(OffsetDateTime offsetDateTime) {
    if (offsetDateTime == null) {
      return null;
    }
    return Date.from(offsetDateTime.toInstant());
  }
}
