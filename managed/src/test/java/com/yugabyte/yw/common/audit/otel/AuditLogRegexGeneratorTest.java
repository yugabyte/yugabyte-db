// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.audit.otel;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import java.util.regex.Pattern;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AuditLogRegexGeneratorTest extends FakeDBApplication {

  private AuditLogRegexGenerator auditLogRegexGenerator;

  @Before
  public void setUp() {
    auditLogRegexGenerator = app.injector().instanceOf(AuditLogRegexGenerator.class);
  }

  @Test
  public void testNormalLogPrefix() {
    String logPrefix =
        "%%a=%a | %%u=%u | %%d=%d | %%r=%r | %%h=%h | %%p=%p | %%t=%t | %%m=%m |"
            + " %%n=%n | %%i=%i | %%e=%e | %%c=%c | %%l=%l | %%s=%s | %%v=%v | %%x=%x | %%C=%C |"
            + " %%R=%R | %%Z=%Z | %%U=%U | %%N=%N | %%H=%H : ";
    AuditLogRegexGenerator.LogRegexResult result =
        auditLogRegexGenerator.generateAuditLogRegex(logPrefix, /*onlyPrefix*/ false);
    assertThat(
        result.getRegex(),
        equalTo(
            "[%][a][=](?P<application_name>.+)[ ][|][ ][%][u][=](?P<user_name>[^ ]+)[ ][|]["
                + " ][%][d][=](?P<database_name>[^ ]+)[ ][|][ ][%][r][=](?P<remote_host_port>[^"
                + " ]+)[ ][|][ ][%][h][=](?P<remote_host>[^ ]+)[ ][|]["
                + " ][%][p][=](?P<process_id>\\d+)[ ][|]["
                + " ][%][t][=](?P<timestamp_without_ms>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}"
                + " \\w{3})[ ][|][ ][%][m][=](?P<timestamp_with_ms>\\d{4}-\\d{2}-\\d{2}"
                + " \\d{2}:\\d{2}:\\d{2}[.]\\d{3} \\w{3})[ ][|]["
                + " ][%][n][=](?P<timestamp_with_ms_epoch>\\d{10}[.]\\d{3})[ ][|]["
                + " ][%][i][=](?P<command_tag>\\w+)[ ][|][ ][%][e][=](?P<error_code>\\d+)[ ][|]["
                + " ][%][c][=](?P<session_id>[^ ]+)[ ][|][ ][%][l][=](?P<log_line_number>\\d+)["
                + " ][|][ ][%][s][=](?P<process_start_timestamp>\\d{4}-\\d{2}-\\d{2}"
                + " \\d{2}:\\d{2}:\\d{2} \\w{3})[ ][|][ ][%][v][=](?P<virtual_transaction_id>[^"
                + " ]+)[ ][|][ ][%][x][=](?P<transaction_id>[^ ]+)[ ][|]["
                + " ][%][C][=](?P<cloud_name>[^ ]+)[ ][|][ ][%][R][=](?P<region>[^ ]+)[ ][|]["
                + " ][%][Z][=](?P<availability_zone>[^ ]+)[ ][|][ ][%][U][=](?P<cluster_uuid>[^"
                + " ]+)[ ][|][ ][%][N][=](?P<node_cluster_name>[^ ]+)[ ][|]["
                + " ][%][H][=](?P<current_hostname>[^ ]+)[ ][:][ ](?P<log_level>\\w+):"
                + "  (?:[0-9A-Z]{5}: )?AUDIT:"
                + " (?P<audit_type>\\w+),(?P<statement_id>\\d+),"
                + "(?P<substatement_id>\\d+),(?P<class>\\w+),(?P<command>[^,]+),"
                + "(?P<object_type>[^,]*),(?P<object_name>[^,]*),(?P<statement>(.|\\n|\\r|\\s)*)"));
    assertThat(
        result.getTokens(),
        contains(
            AuditLogRegexGenerator.LogPrefixTokens.APPLICATION_NAME,
            AuditLogRegexGenerator.LogPrefixTokens.USER_NAME,
            AuditLogRegexGenerator.LogPrefixTokens.DATABASE_NAME,
            AuditLogRegexGenerator.LogPrefixTokens.REMOTE_HOST_PORT,
            AuditLogRegexGenerator.LogPrefixTokens.REMOTE_HOST,
            AuditLogRegexGenerator.LogPrefixTokens.PROCESS_ID,
            AuditLogRegexGenerator.LogPrefixTokens.TIMESTAMP_WITHOUT_MS,
            AuditLogRegexGenerator.LogPrefixTokens.TIMESTAMP_WITH_MS,
            AuditLogRegexGenerator.LogPrefixTokens.TIMESTAMP_WITH_MS_EPOCH,
            AuditLogRegexGenerator.LogPrefixTokens.COMMAND_TAG,
            AuditLogRegexGenerator.LogPrefixTokens.ERROR_CODE,
            AuditLogRegexGenerator.LogPrefixTokens.SESSION_ID,
            AuditLogRegexGenerator.LogPrefixTokens.LOG_LINE_NUMBER,
            AuditLogRegexGenerator.LogPrefixTokens.PROCESS_START_TIMESTAMP,
            AuditLogRegexGenerator.LogPrefixTokens.VIRTUAL_TRANSACTION_ID,
            AuditLogRegexGenerator.LogPrefixTokens.TRANSACTION_ID,
            AuditLogRegexGenerator.LogPrefixTokens.CLOUD_NAME,
            AuditLogRegexGenerator.LogPrefixTokens.REGION,
            AuditLogRegexGenerator.LogPrefixTokens.AVAILABILITY_ZONE,
            AuditLogRegexGenerator.LogPrefixTokens.CLUSTER_UUID,
            AuditLogRegexGenerator.LogPrefixTokens.NODE_CLUSTER_NAME,
            AuditLogRegexGenerator.LogPrefixTokens.CURRENT_HOSTNAME));
  }

  @Test
  public void testPrefixWithNotSeparatedTags() {
    String logPrefix = "%t | %u%d : ";
    AuditLogRegexGenerator.LogRegexResult result =
        auditLogRegexGenerator.generateAuditLogRegex(logPrefix, /*onlyPrefix*/ false);
    assertThat(
        result.getRegex(),
        equalTo(
            "(?P<timestamp_without_ms>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}"
                + " \\w{3})[ ][|][ ]([^ ]+)[ ][:][ ](?P<log_level>\\w+):  (?:[0-9A-Z]{5}: )?AUDIT:"
                + " (?P<audit_type>\\w+),(?P<statement_id>\\d+),(?P<substatement_id>\\d+),"
                + "(?P<class>\\w+),(?P<command>[^,]+),(?P<object_type>[^,]*),"
                + "(?P<object_name>[^,]*),(?P<statement>(.|\\n|\\r|\\s)*)"));
    assertThat(
        result.getTokens(), contains(AuditLogRegexGenerator.LogPrefixTokens.TIMESTAMP_WITHOUT_MS));
  }

  // PLAT-22206: VERBOSE verbosity reads "LOG:  00000: AUDIT: ..."; both forms must parse as audit,
  // otherwise they leak into the query log receiver unparsed.
  @Test
  public void testMatchesBothDefaultAndVerboseErrorVerbosity() {
    Pattern pattern = compile("%m %p ");
    String tail = "AUDIT: SESSION,1,1,DDL,CREATE TABLE,TABLE,public.t,create table t(i int);";
    String prefix = "2026-04-19 15:26:08.973 UTC 10011 LOG:  ";

    assertTrue("default verbosity audit line must match", matches(pattern, prefix + tail));
    assertTrue("VERBOSE audit line must match", matches(pattern, prefix + "00000: " + tail));
    assertTrue(
        "multi-line VERBOSE audit line must match",
        matches(pattern, prefix + "00000: " + tail + "\n  and a second line;"));
  }

  @Test
  public void testDoesNotMatchNonAuditLines() {
    Pattern pattern = compile("%m %p ");
    String prefix = "2026-04-19 15:26:08.973 UTC 10011 ";

    assertFalse(matches(pattern, prefix + "LOG:  statement: select 1;"));
    assertFalse(matches(pattern, prefix + "LOG:  00000: statement: select 1;"));
    assertFalse(matches(pattern, prefix + "LOCATION:  log_audit_event, pgaudit.c:713"));
  }

  // The collector runs Go RE2; java.util.regex rejects the underscores in these group names, hence
  // the flattening. The log prefix stays bracket-free for the same reason - RE2 reads the generated
  // "[[]" / "[]]" as literal brackets, Java does not.
  private Pattern compile(String logPrefix) {
    String regex =
        auditLogRegexGenerator.generateAuditLogRegex(logPrefix, /*onlyPrefix*/ false).getRegex();
    return Pattern.compile(regex.replaceAll("\\(\\?P<\\w+>", "("));
  }

  private boolean matches(Pattern pattern, String logLine) {
    return pattern.matcher(logLine).matches();
  }
}
