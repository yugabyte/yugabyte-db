// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.alerts;

import static com.yugabyte.yw.common.TestUtils.replaceFirstChar;
import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowMinusWithoutMillis;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowPlusWithoutMillis;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.filters.AlertConfigurationApiFilter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MaintenanceWindow;
import com.yugabyte.yw.models.MaintenanceWindow.SortBy;
import com.yugabyte.yw.models.MaintenanceWindow.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MaintenanceWindowFilter;
import com.yugabyte.yw.models.paging.MaintenanceWindowPagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import java.io.IOException;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import java.util.List;
import java.util.function.Consumer;
import junitparams.JUnitParamsRunner;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class MaintenanceServiceTest extends FakeDBApplication {

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  private Customer customer;
  private Universe universe;

  private Date startDate = nowPlusWithoutMillis(1, ChronoUnit.HOURS);
  private Date endDate = nowPlusWithoutMillis(2, ChronoUnit.HOURS);

  private MaintenanceService maintenanceService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("Customer");
    universe = ModelFactory.createUniverse();

    maintenanceService = app.injector().instanceOf(MaintenanceService.class);
  }

  @Test
  public void testSerialization() throws IOException {
    String initial = TestUtils.readResource("alert/maintenance_window.json");

    JsonNode initialJson = Json.parse(initial);

    MaintenanceWindow window = Json.fromJson(initialJson, MaintenanceWindow.class);

    JsonNode resultJson = Json.toJson(window);

    assertThat(resultJson, equalTo(initialJson));
  }

  @Test
  public void testAddAndGetByUUID() {
    MaintenanceWindow window = createAndSaveTestWindow();

    MaintenanceWindow queriedWindow = maintenanceService.get(window.getUuid());

    assertTestWindow(queriedWindow);
  }

  @Test
  public void testUpdateAndQueryByState() {
    MaintenanceWindow window = createAndSaveTestWindow();
    MaintenanceWindow window2 = createAndSaveTestWindow();

    AlertConfigurationApiFilter filter = new AlertConfigurationApiFilter();
    window.setName("Updated");
    window.setStartTime(nowMinusWithoutMillis(1, ChronoUnit.HOURS));
    window.setAlertConfigurationFilter(filter);

    maintenanceService.save(window);

    List<MaintenanceWindow> queriedWindows =
        maintenanceService.list(MaintenanceWindowFilter.builder().state(State.ACTIVE).build());

    assertThat(queriedWindows, hasSize(1));
    MaintenanceWindow queries = queriedWindows.get(0);
    assertThat(queries.getAlertConfigurationFilter(), equalTo(filter));
    assertThat(queries.getName(), equalTo("Updated"));
  }

  @Test
  public void testFilter() {
    MaintenanceWindow window = createAndSaveTestWindow();
    MaintenanceWindow window2 = createAndSaveTestWindow();

    window.setStartTime(nowMinusWithoutMillis(1, ChronoUnit.HOURS));

    maintenanceService.save(window);

    // Empty filter
    MaintenanceWindowFilter filter = MaintenanceWindowFilter.builder().build();
    assertFind(filter, window, window2);

    // Uuid filter
    filter = MaintenanceWindowFilter.builder().uuid(window.getUuid()).build();
    assertFind(filter, window);

    // State filter
    filter = MaintenanceWindowFilter.builder().state(State.PENDING).build();
    assertFind(filter, window2);
  }

  private void assertFind(MaintenanceWindowFilter filter, MaintenanceWindow... windows) {
    List<MaintenanceWindow> queried = maintenanceService.list(filter);
    assertThat(queried, hasSize(windows.length));
    assertThat(queried, containsInAnyOrder(windows));
  }

  @Test
  public void testSort() {
    MaintenanceWindow window = createTestWindow();
    MaintenanceWindow window2 = createTestWindow();
    MaintenanceWindow window3 = createTestWindow();

    window.generateUUID();
    window.setUuid(replaceFirstChar(window.getUuid(), 'a'));
    window.setName("Updated");
    window.setStartTime(nowMinusWithoutMillis(1, ChronoUnit.HOURS));
    window.setEndTime(nowPlusWithoutMillis(4, ChronoUnit.HOURS));
    window.setCreateTime(nowMinusWithoutMillis(1, ChronoUnit.HOURS));
    window.save();

    window2.generateUUID();
    window2.setUuid(replaceFirstChar(window2.getUuid(), 'b'));
    window2.setCreateTime(nowWithoutMillis());
    window2.save();

    window3.generateUUID();
    window3.setUuid(replaceFirstChar(window3.getUuid(), 'c'));
    window3.setName("Aaaa");
    window3.setStartTime(nowMinusWithoutMillis(5, ChronoUnit.HOURS));
    window3.setEndTime(nowMinusWithoutMillis(4, ChronoUnit.HOURS));
    window3.setCreateTime(nowMinusWithoutMillis(6, ChronoUnit.HOURS));
    window3.save();

    maintenanceService.save(window);

    assertSort(SortBy.uuid, SortDirection.ASC, window, window2, window3);
    assertSort(SortBy.name, SortDirection.DESC, window, window2, window3);
    assertSort(SortBy.createTime, SortDirection.ASC, window3, window, window2);
    assertSort(SortBy.startTime, SortDirection.DESC, window2, window, window3);
    assertSort(SortBy.endTime, SortDirection.ASC, window3, window2, window);
    assertSort(SortBy.state, SortDirection.ASC, window2, window, window3);
  }

  private void assertSort(SortBy sortBy, SortDirection direction, MaintenanceWindow... windows) {
    MaintenanceWindowFilter filter = MaintenanceWindowFilter.builder().build();
    MaintenanceWindowPagedQuery query = new MaintenanceWindowPagedQuery();
    query.setFilter(filter);
    query.setSortBy(sortBy);
    query.setDirection(direction);
    query.setLimit(3);
    List<MaintenanceWindow> queried = maintenanceService.pagedList(query).getEntities();
    assertThat(queried, hasSize(windows.length));
    assertThat(queried, contains(windows));
  }

  @Test
  public void testDelete() {
    MaintenanceWindow window = createAndSaveTestWindow();

    maintenanceService.delete(window.getUuid());

    MaintenanceWindow queried = maintenanceService.get(window.getUuid());

    assertThat(queried, nullValue());
  }

  @Test
  public void testValidation() {
    testValidation(
        window -> window.setCustomerUUID(null),
        "errorJson: {\"customerUUID\":[\"must not be null\"]}");

    testValidation(window -> window.setName(null), "errorJson: {\"name\":[\"must not be null\"]}");

    testValidation(
        window -> window.setName(StringUtils.repeat("a", 1001)),
        "errorJson: {\"name\":[\"size must be between 1 and 1000\"]}");

    testValidation(
        window -> window.setStartTime(null), "errorJson: {\"startTime\":[\"must not be null\"]}");

    testValidation(
        window -> window.setEndTime(null), "errorJson: {\"endTime\":[\"must not be null\"]}");

    testValidation(
        window -> window.setStartTime(nowPlusWithoutMillis(10, ChronoUnit.HOURS)),
        "errorJson: {\"endTime\":[\"should be after startTime\"]}");

    testValidation(
        window -> window.setAlertConfigurationFilter(null),
        "errorJson: {\"alertConfigurationFilter\":[\"must not be null\"]}");
  }

  private void testValidation(Consumer<MaintenanceWindow> modifier, String expectedMessage) {
    MaintenanceWindow window = createTestWindow();
    modifier.accept(window);

    assertThat(
        () -> maintenanceService.save(window),
        thrown(PlatformServiceException.class, expectedMessage));
  }

  private MaintenanceWindow createTestWindow() {
    AlertConfigurationApiFilter filter = new AlertConfigurationApiFilter();
    filter.setName("Replication Lag");
    MaintenanceWindow window =
        new MaintenanceWindow()
            .setName("Test")
            .setDescription("Test Description")
            .setStartTime(startDate)
            .setEndTime(endDate)
            .setCustomerUUID(customer.getUuid())
            .setAlertConfigurationFilter(filter);
    return window;
  }

  private MaintenanceWindow createAndSaveTestWindow() {
    return maintenanceService.save(createTestWindow());
  }

  private void assertTestWindow(MaintenanceWindow configuration) {
    AlertConfigurationApiFilter filter = new AlertConfigurationApiFilter();
    filter.setName("Replication Lag");
    assertThat(configuration.getCustomerUUID(), equalTo(customer.getUuid()));
    assertThat(configuration.getName(), equalTo("Test"));
    assertThat(configuration.getDescription(), equalTo("Test Description"));
    assertThat(configuration.getStartTime(), equalTo(startDate));
    assertThat(configuration.getEndTime(), equalTo(endDate));
    assertThat(configuration.getAlertConfigurationFilter(), equalTo(filter));
    assertThat(configuration.getState(), equalTo(State.PENDING));
    assertThat(configuration.getUuid(), notNullValue());
    assertThat(configuration.getCreateTime(), notNullValue());
  }
}
