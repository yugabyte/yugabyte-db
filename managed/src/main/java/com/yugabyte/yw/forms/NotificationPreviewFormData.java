package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.AlertChannelTemplates;
import java.util.UUID;
import javax.validation.constraints.NotNull;
import lombok.Data;

@Data
public class NotificationPreviewFormData {

  @NotNull private UUID alertConfigUuid;

  @NotNull private AlertChannelTemplates alertChannelTemplates;
}
