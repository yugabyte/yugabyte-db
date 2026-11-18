// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.Applied;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.CpuFallback;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.Limits;
import java.util.Collections;
import java.util.List;
import org.junit.Test;

public class SupportBundleV2LimitsExecTest {

  private static final String SCRIPT = "/tmp/yba-support-bundle-x/yb-sb-limits-exec.sh";
  private static final String TRAILER =
      "\nWRAPPER_LIMITS cpu_mode=quota cpu_max=15000/100000 memory_high=30720000"
          + " memory_max=51200000 unit=yb-sb-1a2b3c4d-gflags-x7k2"
          + " slice=ybsupportbundle-throttled.slice reason=none\n";

  @Test
  public void argvPassesEveryLimitAndTheCommandAfterExec() {
    Limits limits =
        new Limits(
            15,
            3,
            5,
            330,
            "yb-sb-1a2b3c4d-gflags-x7k2",
            CpuFallback.FREEZE,
            false,
            SupportBundleV2LimitsExec.SLICE);
    List<String> argv =
        SupportBundleV2LimitsExec.argv(
            SCRIPT, limits, List.of("bash", "/tmp/node_utils.sh", "create_gflags_bundle"));
    assertEquals(
        List.of(
            "bash",
            SCRIPT,
            "--cpu_percent",
            "15",
            "--memory_high_percent",
            "3",
            "--memory_max_percent",
            "5",
            "--runtime_max_secs",
            "330",
            "--unit",
            "yb-sb-1a2b3c4d-gflags-x7k2",
            "--slice",
            "ybsupportbundle.slice",
            "--cpu_fallback",
            "freeze",
            "--exec",
            "bash",
            "/tmp/node_utils.sh",
            "create_gflags_bundle"),
        argv);
  }

  @Test
  public void argvLeavesOutUnsetLimits() {
    Limits limits = new Limits(0, 0, 0, 0, "", CpuFallback.NONE, false, "");
    assertEquals(
        List.of("bash", SCRIPT, "--cpu_fallback", "none", "--exec", "true"),
        SupportBundleV2LimitsExec.argv(SCRIPT, limits, List.of("true")));
  }

  @Test
  public void argvAsksForNoCgroup() {
    Limits limits = new Limits(15, 3, 5, 60, null, CpuFallback.NONE, true, null);
    List<String> argv = SupportBundleV2LimitsExec.argv(SCRIPT, limits, List.of("true"));
    assertEquals("--no_cgroup", argv.get(argv.indexOf("--exec") - 1));
  }

  @Test
  public void argvKeepsCommandArgumentsWhole() {
    Limits limits = new Limits(15, 0, 0, 0, null, CpuFallback.NONE, false, null);
    List<String> command = List.of("ysqlsh", "-c", "select 'a b', \"c\" from t;", "*");
    List<String> argv = SupportBundleV2LimitsExec.argv(SCRIPT, limits, command);
    assertEquals(command, argv.subList(argv.indexOf("--exec") + 1, argv.size()));
  }

  @Test
  public void argvNeedsACommand() {
    Limits limits = new Limits(15, 0, 0, 0, null, CpuFallback.NONE, false, null);
    assertThrows(
        IllegalArgumentException.class,
        () -> SupportBundleV2LimitsExec.argv(SCRIPT, limits, Collections.emptyList()));
  }

  @Test
  public void parseTrailerReadsEveryField() {
    Applied applied = SupportBundleV2LimitsExec.parseTrailer("out\n" + TRAILER).orElseThrow();
    assertEquals(
        new Applied(
            "quota",
            "15000/100000",
            "30720000",
            "51200000",
            "yb-sb-1a2b3c4d-gflags-x7k2",
            "none",
            "ybsupportbundle-throttled.slice"),
        applied);
  }

  @Test
  public void parseTrailerAfterOutputWithoutNewline() {
    String output =
        "abc\nWRAPPER_LIMITS cpu_mode=none cpu_max=none memory_high=none memory_max=none"
            + " unit=none reason=not_cgroup_v2";
    Applied applied = SupportBundleV2LimitsExec.parseTrailer(output).orElseThrow();
    assertEquals("none", applied.cpuMode());
    assertEquals("not_cgroup_v2", applied.reason());
  }

  @Test
  public void parseTrailerDefaultsMissingFieldsToNone() {
    Applied applied =
        SupportBundleV2LimitsExec.parseTrailer("\nWRAPPER_LIMITS cpu_mode=freeze\n").orElseThrow();
    assertEquals("freeze", applied.cpuMode());
    assertEquals("none", applied.memoryMax());
    assertEquals("none", applied.slice());
  }

  @Test
  public void parseTrailerIgnoresOutputWithoutOne() {
    assertFalse(SupportBundleV2LimitsExec.parseTrailer("plain output\n").isPresent());
    assertFalse(SupportBundleV2LimitsExec.parseTrailer(null).isPresent());
  }

  @Test
  public void parseTrailerIgnoresALineThatIsNotLast() {
    String output = "WRAPPER_LIMITS cpu_mode=quota\nmore output\n";
    assertFalse(SupportBundleV2LimitsExec.parseTrailer(output).isPresent());
  }

  @Test
  public void parseTrailerIgnoresTheMarkerMidLine() {
    assertFalse(
        SupportBundleV2LimitsExec.parseTrailer("echo WRAPPER_LIMITS cpu_mode=quota\n").isPresent());
  }

  @Test
  public void stripTrailerRestoresTheCommandOutput() {
    assertEquals("out\n", SupportBundleV2LimitsExec.stripTrailer("out\n" + TRAILER));
    assertEquals("abc", SupportBundleV2LimitsExec.stripTrailer("abc" + TRAILER));
    assertEquals("", SupportBundleV2LimitsExec.stripTrailer(TRAILER));
    assertEquals(
        "Command output:\nout\n",
        SupportBundleV2LimitsExec.stripTrailer("Command output:\nout\n" + TRAILER));
  }

  @Test
  public void stripTrailerLeavesOtherOutputAlone() {
    String output = "WRAPPER_LIMITS cpu_mode=quota\nmore output\n";
    assertEquals(output, SupportBundleV2LimitsExec.stripTrailer(output));
  }
}
