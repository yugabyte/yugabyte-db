// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.SafeConstructor;
import org.yaml.snakeyaml.nodes.MappingNode;
import org.yaml.snakeyaml.nodes.Node;
import org.yaml.snakeyaml.nodes.NodeTuple;
import org.yaml.snakeyaml.nodes.ScalarNode;
import org.yaml.snakeyaml.nodes.SequenceNode;
import play.libs.Json;

/**
 * Redacts the LDAP secrets in Kubernetes overrides (userIntent universeOverrides and azOverrides)
 * for API responses, and puts the stored values back when overrides copied from a response are
 * submitted. Only the secret scalars are rewritten, located by parsing the YAML, so the rest of the
 * user's text, comments included, is kept: the UI shows it as typed, and overrides sent back
 * unchanged equal redact(stored) exactly.
 */
@Slf4j
public final class KubernetesOverridesRedactor {

  private static final String REDACTED = RedactingService.SECRET_REPLACEMENT;

  private KubernetesOverridesRedactor() {}

  private static class Secret {
    final List<String> path;
    final String key;
    final ScalarNode value;

    Secret(List<String> path, String key, ScalarNode value) {
      this.path = path;
      this.key = key;
      this.value = value;
    }

    boolean isHba() {
      return GFlagsUtil.YSQL_HBA_CONF_CSV.equals(key);
    }
  }

  private static class Replacement {
    final int start;
    final int end;
    final String text;

    Replacement(int start, int end, String text) {
      this.start = start;
      this.end = end;
      this.text = text;
    }
  }

  /** Thrown when the text is not one YAML document whose secrets are all scalars. */
  private static class UnparsableException extends Exception {
    UnparsableException(String message, Throwable cause) {
      super(message, cause);
    }
  }

  /**
   * Whole-value secrets: the names RedactingService redacts in text when no DB version is given, as
   * PlatformResults redacts API responses.
   */
  private static Set<String> secretKeys() {
    return RedactingService.sensitiveNames(null, null);
  }

  public static String redact(String overrides) {
    if (overrides == null
        || (secretKeys().stream().noneMatch(overrides::contains)
            && !HbaConfRedactor.mentionsLdapBindPasswd(overrides))) {
      return overrides;
    }
    try {
      List<Replacement> replacements = new ArrayList<>();
      for (Secret secret : findSecrets(overrides)) {
        String value = secret.value.getValue();
        if (secret.isHba()) {
          String redacted = HbaConfRedactor.redact(value);
          if (!redacted.equals(value)) {
            replacements.add(replaceScalar(overrides, secret.value, quote(redacted)));
          }
        } else if (!value.isEmpty() && !REDACTED.equals(value)) {
          // An empty value has no span to replace; REDACTED would be glued to the key's colon.
          replacements.add(replaceScalar(overrides, secret.value, REDACTED));
        }
      }
      return apply(overrides, replacements);
    } catch (UnparsableException e) {
      // Not one YAML document with scalar secrets; the text scan errs towards redacting too much.
      log.debug("Redacting Kubernetes overrides as text: {}", e.getMessage());
      return RedactingService.redactSensitiveInfoInString(overrides);
    }
  }

  /**
   * Returns the submitted overrides with each REDACTED secret replaced by the stored value at the
   * same YAML path, following GFlagsUtil.mergeSensitiveGFlags: a 400 when nothing is stored there
   * or the stored value is itself REDACTED, and for ysql_hba_conf_csv also when its other fields
   * differ from the stored value. stored is null for a new universe or cluster.
   */
  public static String restore(String stored, String submitted, String where) {
    if (submitted == null || !submitted.contains(REDACTED)) {
      return submitted;
    }
    if (stored != null && submitted.equals(redact(stored))) {
      return stored;
    }
    List<Secret> submittedSecrets;
    try {
      submittedSecrets = findSecrets(submitted);
    } catch (UnparsableException e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s contain %s but cannot be parsed to restore the stored values: %s",
              where, REDACTED, e.getMessage()));
    }
    Map<List<String>, Secret> storedSecrets = new HashMap<>();
    String storedParseError = null;
    if (stored != null) {
      try {
        findSecrets(stored).forEach(s -> storedSecrets.put(s.path, s));
      } catch (UnparsableException e) {
        storedParseError = e.getMessage();
      }
    }
    List<Replacement> replacements = new ArrayList<>();
    for (Secret secret : submittedSecrets) {
      String value = secret.value.getValue();
      boolean redacted = secret.isHba() ? value.contains(REDACTED) : REDACTED.equals(value);
      if (!redacted) {
        continue;
      }
      String name = where + " at " + String.join(".", secret.path);
      Secret storedSecret = storedSecrets.get(secret.path);
      String storedValue = storedSecret == null ? null : storedSecret.value.getValue();
      if (storedValue == null && storedParseError != null) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s contain %s, and the stored overrides cannot be parsed to restore it from: %s."
                    + " Submit the actual value.",
                name, REDACTED, storedParseError));
      }
      if (storedValue == null) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s contain %s, and the universe stores no value at the same place to restore it"
                    + " from. Submit the actual value.",
                name, REDACTED));
      }
      if (storedValue.contains(REDACTED)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s contain %s, and the stored value is %s too, so the real value cannot be"
                    + " recovered. Submit the actual value.",
                name, REDACTED, REDACTED));
      }
      if (secret.isHba() && !HbaConfRedactor.redact(storedValue).equals(value)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s contain %s in ldapbindpasswd but differ from the stored value in other"
                    + " fields. To change them, submit the value with the actual ldapbindpasswd.",
                name, REDACTED));
      }
      replacements.add(
          replaceScalar(submitted, secret.value, storedScalarText(stored, storedSecret.value)));
    }
    return apply(submitted, replacements);
  }

  /** restore() for each AZ's overrides, against the stored overrides of the same AZ. */
  public static Map<String, String> restoreAz(
      Map<String, String> stored, Map<String, String> submitted) {
    if (submitted == null) {
      return null;
    }
    Map<String, String> restored = new LinkedHashMap<>();
    submitted.forEach(
        (az, overrides) ->
            restored.put(
                az,
                restore(
                    stored == null ? null : stored.get(az),
                    overrides,
                    "Kubernetes overrides of AZ " + az)));
    return restored;
  }

  private static List<Secret> findSecrets(String overrides) throws UnparsableException {
    Node root;
    try {
      root =
          new Yaml(new SafeConstructor(new LoaderOptions())).compose(new StringReader(overrides));
    } catch (RuntimeException e) {
      throw new UnparsableException(e.getMessage(), e);
    }
    List<Secret> secrets = new ArrayList<>();
    if (root != null) {
      collect(root, new ArrayList<>(), secretKeys(), secrets, new HashSet<>(), new HashSet<>());
    }
    return secrets;
  }

  /**
   * An alias reuses the node of its anchor, so a secret behind an alias is rewritten once, where
   * the anchor is. Secret nodes are tracked apart from visited, so a secret is still found when its
   * anchor sits under a key that is not secret.
   */
  private static void collect(
      Node node,
      List<String> path,
      Set<String> secretKeys,
      List<Secret> secrets,
      Set<Node> visited,
      Set<Node> secretNodes)
      throws UnparsableException {
    if (!visited.add(node)) {
      return;
    }
    if (node instanceof MappingNode) {
      for (NodeTuple tuple : ((MappingNode) node).getValue()) {
        if (!(tuple.getKeyNode() instanceof ScalarNode)) {
          continue;
        }
        String key = ((ScalarNode) tuple.getKeyNode()).getValue();
        List<String> childPath = new ArrayList<>(path);
        childPath.add(key);
        Node value = tuple.getValueNode();
        if (secretKeys.contains(key) || GFlagsUtil.YSQL_HBA_CONF_CSV.equals(key)) {
          if (!(value instanceof ScalarNode)) {
            throw new UnparsableException(key + " is not a scalar", null);
          }
          if (secretNodes.add(value)) {
            secrets.add(new Secret(childPath, key, (ScalarNode) value));
          }
        } else {
          collect(value, childPath, secretKeys, secrets, visited, secretNodes);
        }
      }
    } else if (node instanceof SequenceNode) {
      List<Node> items = ((SequenceNode) node).getValue();
      for (int i = 0; i < items.size(); i++) {
        List<String> childPath = new ArrayList<>(path);
        childPath.add(String.valueOf(i));
        collect(items.get(i), childPath, secretKeys, secrets, visited, secretNodes);
      }
    }
  }

  /**
   * Replaces a scalar, whatever its style, with text that fits on its first line. A block scalar
   * ends after its line breaks, which are kept so the next key stays on a line of its own.
   */
  private static Replacement replaceScalar(String text, ScalarNode scalar, String replacement) {
    int start = scalarStart(text, scalar);
    int end = text.offsetByCodePoints(0, scalar.getEndMark().getIndex());
    int contentEnd = end;
    while (contentEnd > start
        && (text.charAt(contentEnd - 1) == '\n' || text.charAt(contentEnd - 1) == '\r')) {
      contentEnd--;
    }
    return new Replacement(start, end, replacement + text.substring(contentEnd, end));
  }

  /**
   * The stored scalar as the user wrote it, when it sits on one line; a multi-line scalar depends
   * on its indentation, so it is quoted instead.
   */
  private static String storedScalarText(String stored, ScalarNode scalar) {
    int start = scalarStart(stored, scalar);
    int end = stored.offsetByCodePoints(0, scalar.getEndMark().getIndex());
    String text = stored.substring(start, end);
    return text.indexOf('\n') < 0 && text.indexOf('\r') < 0 ? text : quote(scalar.getValue());
  }

  /**
   * Where the scalar's own text starts. SnakeYAML counts code points, and starts the mark at an
   * &anchor or !tag in front of the scalar, which is kept so aliases still resolve. A scalar cannot
   * start with & or ! itself: plain scalars may not, and the other styles start with a quote, | or
   * >.
   */
  private static int scalarStart(String text, ScalarNode scalar) {
    int start = text.offsetByCodePoints(0, scalar.getStartMark().getIndex());
    // An empty scalar ends at its properties; the skip must not run on into the next line.
    int end = text.offsetByCodePoints(0, scalar.getEndMark().getIndex());
    while (start < end && (text.charAt(start) == '&' || text.charAt(start) == '!')) {
      while (start < end && !Character.isWhitespace(text.charAt(start))) {
        start++;
      }
      while (start < end && Character.isWhitespace(text.charAt(start))) {
        start++;
      }
    }
    return start;
  }

  /** A JSON string is a YAML double-quoted scalar. */
  private static String quote(String value) {
    try {
      return Json.mapper().writeValueAsString(value);
    } catch (JsonProcessingException e) {
      throw new IllegalStateException(e);
    }
  }

  private static String apply(String text, List<Replacement> replacements) {
    replacements.sort(Comparator.comparingInt((Replacement r) -> r.start).reversed());
    StringBuilder out = new StringBuilder(text);
    for (Replacement r : replacements) {
      out.replace(r.start, r.end, r.text);
    }
    return out.toString();
  }
}
