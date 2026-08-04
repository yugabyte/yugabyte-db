package com.yugabyte.yw.forms;

import lombok.Data;

@Data
public class NotificationPreview {

  private String title;
  private String text;
  private String highlightedTitle;
  private String highlightedText;
}
