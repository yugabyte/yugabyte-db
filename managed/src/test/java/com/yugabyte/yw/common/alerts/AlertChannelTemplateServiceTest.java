// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static com.yugabyte.yw.common.alerts.AlertTemplateVariableServiceTest.createTestVariable;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertChannelTemplates;
import com.yugabyte.yw.models.AlertTemplateVariable;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class AlertChannelTemplateServiceTest extends FakeDBApplication {

  private UUID defaultCustomerUuid;

  private AlertChannelTemplateService templateService;
  private AlertTemplateVariableService alertTemplateVariableService;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
    templateService = app.injector().instanceOf(AlertChannelTemplateService.class);
    alertTemplateVariableService = app.injector().instanceOf(AlertTemplateVariableService.class);
  }

  @Test
  public void testCreateAndGet() {
    AlertChannelTemplates templates = createTemplates(ChannelType.Email);
    templateService.save(templates);

    AlertChannelTemplates fromDb = templateService.get(defaultCustomerUuid, ChannelType.Email);
    assertThat(fromDb, equalTo(templates));
  }

  @Test
  public void testCreateWithDefaultValue() {
    for (AlertChannelTemplatesExt defaultTemplates :
        templateService.listWithDefaults(defaultCustomerUuid)) {
      AlertChannelTemplates templates = defaultTemplates.getChannelTemplates();
      templates.setTitleTemplate(defaultTemplates.getDefaultTitleTemplate());
      templates.setTextTemplate(defaultTemplates.getDefaultTextTemplate());
      templateService.save(templates);

      AlertChannelTemplates fromDb = templateService.get(defaultCustomerUuid, templates.getType());
      assertThat(fromDb, equalTo(templates));
    }
  }

  @Test
  public void testGetOrBadRequest() {
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              templateService.getOrBadRequest(defaultCustomerUuid, ChannelType.Email);
            });
    assertThat(exception.getMessage(), equalTo("No templates defined for channel type Email"));
  }

  @Test
  public void testList() {
    AlertChannelTemplates templates = createTemplates(ChannelType.Email);
    templateService.save(templates);
    AlertChannelTemplates templates2 = createTemplates(ChannelType.Slack);
    templateService.save(templates2);

    // Second customer with one channel.
    UUID newCustomerUUID = ModelFactory.testCustomer().getUuid();
    AlertChannelTemplates otherCustomerTemplate =
        new AlertChannelTemplates()
            .setType(ChannelType.Email)
            .setCustomerUUID(newCustomerUUID)
            .setTextTemplate("qwewqe");
    templateService.save(otherCustomerTemplate);

    List<AlertChannelTemplates> customerTemplates = templateService.list(defaultCustomerUuid);
    assertThat(customerTemplates, containsInAnyOrder(templates, templates2));
  }

  @Test
  public void testUpdate() {
    AlertChannelTemplates templates = createTemplates(ChannelType.Email);
    templateService.save(templates);

    AlertChannelTemplates updated =
        new AlertChannelTemplates()
            .setCustomerUUID(defaultCustomerUuid)
            .setType(ChannelType.Email)
            .setTextTemplate("newTemplate");
    AlertChannelTemplates updateResult = templateService.save(updated);
    assertThat(updateResult, equalTo(updated));

    AlertChannelTemplates updatedFromDb =
        templateService.get(defaultCustomerUuid, ChannelType.Email);
    assertThat(updatedFromDb, equalTo(updated));
  }

  @Test
  public void testUpdateMissingVariable() {
    AlertChannelTemplates templates = createTemplates(ChannelType.Email);
    templateService.save(templates);

    AlertTemplateVariable variable = createTestVariable(defaultCustomerUuid, "test");
    alertTemplateVariableService.save(variable);

    AlertChannelTemplates updated =
        new AlertChannelTemplates()
            .setCustomerUUID(defaultCustomerUuid)
            .setType(ChannelType.Email)
            .setTitleTemplate("Bla {{ test }} and {{ test1 }} bla bla")
            .setTextTemplate("qwerty");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              templateService.save(updated);
            });
    assertThat(
        exception.getMessage(), equalTo("errorJson: {\"\":[\"variable 'test1' does not exist\"]}"));
  }

  private AlertChannelTemplates createTemplates(ChannelType type) {
    return createTemplates(defaultCustomerUuid, type);
  }

  public static AlertChannelTemplates createTemplates(UUID customerUuid, ChannelType type) {
    return new AlertChannelTemplates()
        .setCustomerUUID(customerUuid)
        .setType(type)
        .setTitleTemplate(type.isHasTitle() ? "titleTemplate" : null)
        .setTextTemplate("textTemplate");
  }
}
