package com.yugabyte.yw.forms;

import java.util.UUID;
import javax.validation.constraints.NotNull;
import lombok.Data;

@Data
public class NotificationPreviewFormData {

  @NotNull private UUID alertConfigUuid;

  @NotNull private AlertChannelTemplatesPreview alertChannelTemplates;
}
