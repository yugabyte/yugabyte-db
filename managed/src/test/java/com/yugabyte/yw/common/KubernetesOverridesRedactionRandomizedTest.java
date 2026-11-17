// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.TreeSet;
import org.junit.Test;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.SafeConstructor;
import play.libs.Json;

/**
 * Random overrides YAML through redact and restore: secret gflags and ysql_hba_conf_csv in every
 * scalar style, in block and flow collections and lists, with comments, escaped keys, a document
 * marker and CRLF line ends, and now and then another string that mentions a secret, which hides
 * the overrides whole. Secrets end with -secret, which nothing else contains, so any output holding
 * it leaks. The redacted YAML must load as the stored YAML with each secret redacted, and restore
 * must give back the stored text.
 */
public class KubernetesOverridesRedactionRandomizedTest {

  private static final List<String> NAMES =
      new ArrayList<>(new TreeSet<>(RedactingService.sensitiveNames(null, null)));

  private Random random;
  private int next;
  private boolean secretInList;
  private boolean hiddenWhole;

  @Test
  public void testRedactAndRestore() {
    for (int seed = 0; seed < 4000; seed++) {
      random = new Random(seed);
      next = 0;
      secretInList = false;
      StringBuilder yaml = new StringBuilder();
      if (random.nextInt(10) == 0) {
        yaml.append("---\n");
      }
      entries(yaml, "", 0);
      String eol = random.nextInt(10) == 0 ? "\r\n" : "\n";
      String stored = yaml.toString().replace("\n", eol);
      String message = "seed " + seed + ":\n" + stored;

      String redacted = KubernetesOverridesRedactor.redact(stored);
      assertFalse(message + "\nredacted:\n" + redacted, redacted.contains("-secret"));
      hiddenWhole = false;
      Object want = expected(load(stored), null);
      if (hiddenWhole) {
        assertEquals(message, RedactingService.SECRET_REPLACEMENT, redacted);
      } else {
        assertEquals(message + "\nredacted:\n" + redacted, want, load(redacted));
      }
      assertEquals(message, stored, KubernetesOverridesRedactor.restore(stored, redacted, "o"));
      if (hiddenWhole) {
        continue;
      }

      String edited = redacted + "zz_extra: 1" + eol;
      if (secretInList && !redacted.equals(stored)) {
        assertThrows(
            message,
            PlatformServiceException.class,
            () -> KubernetesOverridesRedactor.restore(stored, edited, "o"));
        continue;
      }
      @SuppressWarnings("unchecked")
      Map<Object, Object> wantEdited = new LinkedHashMap<>((Map<Object, Object>) load(stored));
      wantEdited.put("zz_extra", 1);
      String restored = KubernetesOverridesRedactor.restore(stored, edited, "o");
      assertEquals(message + "\nrestored:\n" + restored, wantEdited, load(restored));
    }
  }

  private static Object load(String yaml) {
    return new Yaml(new SafeConstructor(new LoaderOptions())).load(yaml);
  }

  /**
   * The loaded YAML with each secret redacted, worked out on the tree rather than the text. Sets
   * hiddenWhole for another string that mentions a secret.
   */
  @SuppressWarnings("unchecked")
  private Object expected(Object node, String key) {
    if (node instanceof Map) {
      Map<Object, Object> out = new LinkedHashMap<>();
      ((Map<Object, Object>) node).forEach((k, v) -> out.put(k, expected(v, String.valueOf(k))));
      return out;
    }
    if (node instanceof List) {
      List<Object> out = new ArrayList<>();
      for (Object item : (List<Object>) node) {
        out.add(expected(item, null));
      }
      return out;
    }
    if (!(node instanceof String)) {
      return node;
    }
    String value = (String) node;
    if (key != null && NAMES.contains(key)) {
      return value.isEmpty() ? value : RedactingService.SECRET_REPLACEMENT;
    }
    if (GFlagsUtil.YSQL_HBA_CONF_CSV.equals(key)) {
      return HbaConfRedactor.redact(value);
    }
    if (NAMES.stream().anyMatch(value::contains) || HbaConfRedactor.mentionsLdapBindPasswd(value)) {
      hiddenWhole = true;
    }
    return value;
  }

  private String secret() {
    return "s" + (next++) + "-secret";
  }

  private String name() {
    return NAMES.get(random.nextInt(NAMES.size()));
  }

  private static String quote(String value) {
    try {
      return Json.mapper().writeValueAsString(value);
    } catch (JsonProcessingException e) {
      throw new IllegalStateException(e);
    }
  }

  /** A secret gflag name as a key, sometimes double-quoted with its first letter escaped. */
  private String secretKey(String name) {
    return random.nextInt(5) == 0
        ? String.format("\"\\x%02x%s\"", (int) name.charAt(0), name.substring(1))
        : name;
  }

  private String headerComment() {
    return random.nextInt(4) == 0 ? "  # ldapbindpasswd=" + secret() : "";
  }

  /** A scalar value in block context, ending its line; block scalars take the lines below. */
  private String blockValue(String value, String indent) {
    switch (random.nextInt(5)) {
      case 0:
        return value + "\n";
      case 1:
        return "'" + value + "'\n";
      case 2:
        return quote(value) + "\n";
      case 3:
        return "|" + headerComment() + "\n" + indent + "  " + value + "\n";
      default:
        return ">-" + headerComment() + "\n" + indent + "  " + value + "\n";
    }
  }

  private String hbaValue() {
    return "host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com ldapbindpasswd="
        + secret()
        + (random.nextBoolean() ? " ldaptls=1" : "");
  }

  private void entries(StringBuilder out, String indent, int depth) {
    Set<String> keys = new HashSet<>();
    int count = 1 + random.nextInt(4);
    for (int i = 0; i < count; i++) {
      switch (random.nextInt(8)) {
        case 0:
          out.append(indent).append("# ").append(name()).append(": ").append(secret()).append("\n");
          break;
        case 1:
          // A commented-out value on the line below its key.
          out.append(indent).append("# ").append(name()).append(":\n");
          out.append(indent).append("#   ").append(secret()).append("\n");
          break;
        default:
      }
      int kind = random.nextInt(depth < 3 ? 10 : 7);
      if (kind <= 1) {
        String name = name();
        if (keys.add(name)) {
          out.append(indent).append(secretKey(name)).append(":");
          switch (random.nextInt(8)) {
            case 0:
              out.append("\n");
              break;
            case 1:
              out.append(" \"\"\n");
              break;
            default:
              out.append(" ").append(blockValue(secret(), indent));
          }
          continue;
        }
      }
      String key = "k" + (next++);
      switch (kind) {
        case 2:
          if (keys.add(GFlagsUtil.YSQL_HBA_CONF_CSV)) {
            out.append(indent)
                .append(GFlagsUtil.YSQL_HBA_CONF_CSV)
                .append(": ")
                .append(blockValue(hbaValue(), indent));
            break;
          }
          // fall through
        case 3:
          out.append(indent)
              .append(key)
              .append(": ")
              .append(
                  blockValue(
                      random.nextInt(8) == 0 ? "--" + name() + "=" + secret() : "v" + (next++),
                      indent));
          break;
        case 4:
          out.append(indent).append(key).append(": test");
          if (random.nextBoolean()) {
            out.append(" # ycql_ldap_bind_passwd=").append(secret());
          }
          out.append("\n");
          break;
        case 5:
          out.append(indent).append(key).append(": {a: test, ").append(name()).append(": ");
          out.append(random.nextBoolean() ? secret() : quote(hbaValue())).append("}\n");
          break;
        case 6:
          out.append(indent).append(key).append(": v").append(next++).append("\n");
          break;
        case 7:
        case 8:
          out.append(indent).append(key).append(":\n");
          entries(out, indent + "  ", depth + 1);
          break;
        default:
          out.append(indent).append(key).append(":\n");
          for (int items = 1 + random.nextInt(3); items > 0; items--) {
            out.append(indent).append("  - ");
            if (random.nextInt(3) == 0) {
              secretInList = true;
              out.append(secretKey(name()))
                  .append(": ")
                  .append(blockValue(secret(), indent + "    "));
            } else {
              out.append("item").append(next++).append("\n");
            }
          }
      }
    }
  }
}
