// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.util;

import static org.yb.AssertionWrappers.assertTrue;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.BaseYBTest;
import org.yb.YBTestRunner;
import org.yb.util.json.Checker;
import org.yb.util.json.ObjectCheckerBuilder;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.PropertyName;
import org.yb.util.json.ValueChecker;

import com.google.gson.JsonElement;
import com.google.gson.JsonParser;

@RunWith(value=YBTestRunner.class)
public class TestJsonUtil extends BaseYBTest {
  private interface CompanyCheckerBuilder extends ObjectCheckerBuilder {
    CompanyCheckerBuilder name(String value);
    CompanyCheckerBuilder locationCountry(String value);
    CompanyCheckerBuilder locationCity(String value);
    CompanyCheckerBuilder establishYear(ValueChecker<Long> checker);
    CompanyCheckerBuilder isPublic(boolean value);
    CompanyCheckerBuilder employees(Checker... checker);
    @PropertyName("EBITDA")
    CompanyCheckerBuilder ebitda(ValueChecker<Double> checker);
    CompanyCheckerBuilder dummy(String value);
  }

  private interface EmployeeCheckerBuilder extends ObjectCheckerBuilder {
    EmployeeCheckerBuilder name(String value);
    EmployeeCheckerBuilder yearOfBirth(ValueChecker<Long> checker);
    EmployeeCheckerBuilder workPosition(String value);
    EmployeeCheckerBuilder weight(ValueChecker<Double> checker);
    EmployeeCheckerBuilder dummy(boolean value);
  }

  private static String convertQuotes(String str) {
    return str.replace('\'', '"');
  }

  private static CompanyCheckerBuilder makeCompanyBuilder() {
    return JsonUtil.makeCheckerBuilder(CompanyCheckerBuilder.class);
  }

  private static EmployeeCheckerBuilder makeEmployeeBuilder() {
    return JsonUtil.makeCheckerBuilder(EmployeeCheckerBuilder.class);
  }

  private static String describeConflicts(Collection<String> conflicts) {
    return "\t" + (conflicts.isEmpty() ? "--none--" : String.join("\n\t", conflicts));
  }

  private static void checkConflicts(
      JsonElement element, Checker checker, String... expectedConflicts) {
    Set<String> unconfirmedConflicts = new HashSet<>();
    Collections.addAll(unconfirmedConflicts, expectedConflicts);
    List<String> conflicts = JsonUtil.findConflicts(element, checker, '\'');
    List<String> unexpectedConflicts = new ArrayList<>();
    for (String conflict : conflicts) {
      if (!unconfirmedConflicts.remove(conflict)) {
        unexpectedConflicts.add(conflict);
      }
    }
    assertTrue(
        String.format(
            "Conflict detection failure\n" +
            "Unexpected conflicts:\n%s\n" +
            "Unconfirmed conflicts:\n%s",
            describeConflicts(unexpectedConflicts),
            describeConflicts(unconfirmedConflicts)),
        unexpectedConflicts.isEmpty() && unconfirmedConflicts.isEmpty());
  }

  private static void checkNoConflicts(JsonElement element, Checker checker) {
    checkConflicts(element, checker);
  }

  @Test
  public void testJsonConflicts() throws Exception {
    JsonElement json = new JsonParser().parse(convertQuotes(
        "{" +
        "  'Name': 'Fairytale and Co'," +
        "  'Location City': 'Village'," +
        "  'Location Country': 'Far Far Away'," +
        "  'Establish Year': 1111," +
        "  'Is Public': false," +
        "  'EBITDA': 56832.34," +
        "  'Dummy': null," +
        "  'Employees':" +
        "    [" +
        "      {" +
        "        'Name': 'John'," +
        "        'Year Of Birth': 1223," +
        "        'Work Position': 'King'," +
        "        'Weight': 160.5" +
        "      }," +
        "      {" +
        "        'Name': 'Marry'," +
        "        'Year Of Birth': 1233," +
        "        'Work Position': 'Queen'," +
        "        'Weight': 110.3," +
        "        'Dummy': null" +
        "      }," +
        "      {" +
        "        'Name': 'Brunhilda'," +
        "        'Year Of Birth': 1133," +
        "        'Work Position': 'Witch'," +
        "        'Weight': 50.9" +
        "      }" +
        "    ]" +
        "}"));
    Checker employeeChecker = makeEmployeeBuilder()
        .weight(Checkers.less(200.0))
        .yearOfBirth(Checkers.greaterOrEqual(1000))
        .build();

    checkConflicts(
        json,
        makeCompanyBuilder()
            .name("Fairytale")
            .employees(
                employeeChecker,
                employeeChecker)
            .build(),
        "'Establish Year': expected to be absent or null",
        "'Location Country': expected to be absent or null",
        "'Employees': 2 items expected but 3 found",
        "'Is Public': expected to be absent or null",
        "'Location City': expected to be absent or null",
        "'EBITDA': expected to be absent or null",
        "'Name': 'Fairytale and Co' is not == 'Fairytale'");

    CompanyCheckerBuilder companyBuilder = makeCompanyBuilder()
        .name("Fairytale and Co")
        .locationCountry("Far Far Away")
        .locationCity("Village")
        .establishYear(Checkers.greaterOrEqual(1100))
        .isPublic(true)
        .ebitda(Checkers.greater(100000.0));

    EmployeeCheckerBuilder johnBuilder = makeEmployeeBuilder()
        .name("John")
        .yearOfBirth(Checkers.equal(1223))
        .workPosition("King")
        .weight(Checkers.equal(150, 0.5));

    EmployeeCheckerBuilder marryBuilder = makeEmployeeBuilder()
        .name("Marry")
        .yearOfBirth(Checkers.greaterOrEqual(1223))
        .workPosition("Queen")
        .weight(Checkers.less(150.0));

    EmployeeCheckerBuilder brunhildaBuilder = makeEmployeeBuilder()
        .name("Brunhilda")
        .yearOfBirth(Checkers.less(1223))
        .weight(Checkers.greater(50.0));

    checkConflicts(
        json,
        companyBuilder
            .employees(
                johnBuilder.build(),
                brunhildaBuilder.build(),
                marryBuilder.build())
            .build(),
        "'Employees'.[0].'Weight': '160.5' is not in '[149.5, 150.5]'",
        "'Employees'.[1].'Work Position': expected to be absent or null",
        "'Employees'.[1].'Year Of Birth': '1233' is not < '1223'",
        "'Employees'.[1].'Name': 'Marry' is not == 'Brunhilda'",
        "'Employees'.[2].'Work Position': 'Witch' is not == 'Queen'",
        "'Employees'.[2].'Year Of Birth': '1133' is not >= '1223'",
        "'Employees'.[2].'Name': 'Brunhilda' is not == 'Marry'",
        "'EBITDA': '56832.34' is not > '100000.0'",
        "'Is Public': 'false' is not == 'true'");

    companyBuilder.isPublic(false).ebitda(Checkers.greater(50000.0));

    checkConflicts(
        json,
        companyBuilder
            .employees(
                johnBuilder.build(),
                marryBuilder.build(),
                brunhildaBuilder.build())
            .build(),
        "'Employees'.[0].'Weight': '160.5' is not in '[149.5, 150.5]'",
        "'Employees'.[2].'Work Position': expected to be absent or null");


    johnBuilder.weight(Checkers.equal(160.2, 0.5));
    brunhildaBuilder.workPosition("Witch");

    checkNoConflicts(
        json,
        companyBuilder
            .employees(
                johnBuilder.build(),
                marryBuilder.build(),
                brunhildaBuilder.build())
            .build());
  }
}
