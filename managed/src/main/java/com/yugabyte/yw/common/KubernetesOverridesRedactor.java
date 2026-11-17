// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.common.cache.CacheStats;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
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
import org.apache.commons.codec.digest.DigestUtils;
import org.yaml.snakeyaml.DumperOptions.ScalarStyle;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.SafeConstructor;
import org.yaml.snakeyaml.error.Mark;
import org.yaml.snakeyaml.error.MarkedYAMLException;
import org.yaml.snakeyaml.nodes.MappingNode;
import org.yaml.snakeyaml.nodes.Node;
import org.yaml.snakeyaml.nodes.NodeTuple;
import org.yaml.snakeyaml.nodes.ScalarNode;
import org.yaml.snakeyaml.nodes.SequenceNode;
import play.libs.Json;

/**
 * Redacts the LDAP secrets in Kubernetes overrides (userIntent universeOverrides and azOverrides)
 * for API responses, and puts the stored values back when overrides copied from a response are
 * submitted. Only the secrets are rewritten, located by parsing the YAML, so the rest of the user's
 * text is kept: the UI shows it as typed, and overrides sent back unchanged equal redact(stored)
 * exactly. A secret is the value of a secret gflag, the ldapbindpasswd fields of ysql_hba_conf_csv,
 * any other string that mentions a secret, and a comment that does; strings and comments are
 * redacted as the logs redact text.
 */
@Slf4j
public final class KubernetesOverridesRedactor {

  private static final String REDACTED = RedactingService.SECRET_REPLACEMENT;

  /**
   * Every universe GET, and each poll of the universe list, redacts the same stored overrides, and
   * parsing those that hold secrets costs about 40 us each. The redacted text is cached under a
   * digest of the overrides, so the cache holds no secret. Bounded at 8M chars (16 MB).
   */
  private static final Cache<String, String> REDACTED_BY_DIGEST =
      CacheBuilder.newBuilder()
          .maximumWeight(8_000_000)
          .<String, String>weigher((digest, redacted) -> digest.length() + redacted.length())
          .recordStats()
          .build();

  private KubernetesOverridesRedactor() {}

  @VisibleForTesting
  static CacheStats cacheStats() {
    return REDACTED_BY_DIGEST.stats();
  }

  private enum Kind {
    /** Under a secret gflag name: the whole value. */
    VALUE,
    /** ysql_hba_conf_csv: the ldapbindpasswd fields. */
    HBA,
    /** Any other string that mentions a secret, redacted as the logs redact text. */
    TEXT;

    String redact(String value) {
      switch (this) {
        case VALUE:
          // An empty value has no span to replace; REDACTED would be glued to the key's colon.
          return value.isEmpty() ? value : REDACTED;
        case HBA:
          return HbaConfRedactor.redact(value);
        default:
          return RedactingService.redactShellProcessOutput(value, RedactionTarget.LOGS);
      }
    }

    boolean holdsRedacted(String value) {
      return this == VALUE ? REDACTED.equals(value) : value.contains(REDACTED);
    }
  }

  private static class Secret {
    final List<String> path;
    final Kind kind;
    final ScalarNode value;
    // The scalar's own text in the overrides, as char offsets.
    final int start;
    final int end;

    Secret(List<String> path, Kind kind, ScalarNode value, int start, int end) {
      this.path = path;
      this.kind = kind;
      this.value = value;
      this.start = start;
      this.end = end;
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

  /**
   * Thrown when the text is not one YAML document whose secret gflags are all scalars. The message
   * never quotes the text, which may hold a secret.
   */
  private static class UnparsableException extends Exception {
    UnparsableException(String message) {
      super(message);
    }
  }

  /**
   * Whole-value secrets: the names RedactingService redacts in text when no DB version is given, as
   * PlatformResults redacts API responses.
   */
  private static Set<String> secretKeys() {
    return RedactingService.sensitiveNames(null, null);
  }

  private static boolean mentionsSecret(String text, Set<String> secretKeys) {
    return secretKeys.stream().anyMatch(text::contains)
        || HbaConfRedactor.mentionsLdapBindPasswd(text);
  }

  public static String redact(String overrides) {
    if (overrides == null || !mentionsSecret(overrides, secretKeys())) {
      return overrides;
    }
    String digest = DigestUtils.sha256Hex(overrides);
    String redacted = REDACTED_BY_DIGEST.getIfPresent(digest);
    if (redacted == null) {
      redacted = redactUncached(overrides);
      REDACTED_BY_DIGEST.put(digest, redacted);
    }
    return redacted;
  }

  private static String redactUncached(String overrides) {
    Finder found;
    try {
      found = find(overrides);
    } catch (UnparsableException e) {
      // The text scan errs towards redacting too much. Ordered as for shell output: the passes that
      // can run on past an unclosed value run one line at a time.
      log.debug("Redacting Kubernetes overrides as text: {}", e.getMessage());
      return RedactingService.redactShellProcessOutput(overrides, RedactionTarget.LOGS);
    }
    List<Replacement> replacements = new ArrayList<>();
    for (Secret secret : found.secrets) {
      String value = secret.value.getValue();
      String redacted = secret.kind.redact(value);
      if (!redacted.equals(value)) {
        replacements.add(
            replaceScalar(
                overrides, secret, secret.kind == Kind.VALUE ? redacted : quote(redacted)));
      }
    }
    // A block scalar's header comment goes with the scalar when the scalar is replaced.
    List<Replacement> scalars = new ArrayList<>(replacements);
    scalars.sort(Comparator.comparingInt((Replacement r) -> r.start));
    for (int[] comment : found.comments()) {
      if (insideAny(scalars, comment[0])) {
        continue;
      }
      String text = overrides.substring(comment[0], comment[1]);
      String redacted = RedactingService.redactShellProcessOutput(text, RedactionTarget.LOGS);
      if (!redacted.equals(text)) {
        replacements.add(new Replacement(comment[0], comment[1], redacted));
      }
    }
    return apply(overrides, replacements);
  }

  /**
   * Returns the submitted overrides with each REDACTED secret replaced by the stored value at the
   * same YAML path, following GFlagsUtil.mergeSensitiveGFlags: a 400 when nothing is stored there
   * or the stored value is itself REDACTED, and for ysql_hba_conf_csv and other strings also when
   * the rest of the value differs from the stored one. Comments are not restored. stored is null
   * for a new universe or cluster.
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
      submittedSecrets = find(submitted).secrets;
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
        find(stored).secrets.forEach(s -> storedSecrets.put(s.path, s));
      } catch (UnparsableException e) {
        storedParseError = e.getMessage();
      }
    }
    List<Replacement> replacements = new ArrayList<>();
    for (Secret secret : submittedSecrets) {
      String value = secret.value.getValue();
      if (!secret.kind.holdsRedacted(value)) {
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
      if (secret.kind != Kind.VALUE && !secret.kind.redact(storedValue).equals(value)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s contain %s but differ from the stored value in the rest of it. To change it,"
                    + " submit the value with the actual secret.",
                name, REDACTED));
      }
      replacements.add(replaceScalar(submitted, secret, storedScalarText(stored, storedSecret)));
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

  private static Finder find(String overrides) throws UnparsableException {
    LoaderOptions options = new LoaderOptions();
    if (overrides.codePointCount(0, overrides.length()) > options.getCodePointLimit()) {
      throw new UnparsableException("longer than " + options.getCodePointLimit() + " code points");
    }
    Node root;
    try {
      root = new Yaml(new SafeConstructor(options)).compose(new StringReader(overrides));
    } catch (MarkedYAMLException e) {
      // Its message quotes the line around the problem, which may hold a secret.
      Mark mark = e.getProblemMark();
      throw new UnparsableException(
          (e.getContext() == null ? "" : e.getContext() + ": ")
              + e.getProblem()
              + (mark == null
                  ? ""
                  : " at line " + (mark.getLine() + 1) + ", column " + (mark.getColumn() + 1)));
    } catch (RuntimeException e) {
      throw new UnparsableException(e.getClass().getSimpleName());
    }
    Finder finder = new Finder(overrides);
    if (root != null) {
      finder.collect(root, new ArrayList<>());
    }
    return finder;
  }

  private static class Finder {
    final String text;
    // SnakeYAML marks count code points: the char offset of each, or null when they are the same.
    final int[] charOffsets;
    final Set<String> secretKeys = secretKeys();
    final List<Secret> secrets = new ArrayList<>();
    final Set<Node> visited = new HashSet<>();
    final Set<Node> secretNodes = new HashSet<>();
    // Every scalar's text, which no comment can be inside of.
    final List<int[]> scalarSpans = new ArrayList<>();

    Finder(String text) {
      this.text = text;
      int codePoints = text.codePointCount(0, text.length());
      if (codePoints == text.length()) {
        charOffsets = null;
      } else {
        charOffsets = new int[codePoints + 1];
        for (int cp = 0, i = 0; cp <= codePoints; cp++) {
          charOffsets[cp] = i;
          if (i < text.length()) {
            i += Character.charCount(text.codePointAt(i));
          }
        }
      }
    }

    int charOffset(int codePointIndex) {
      return charOffsets == null ? codePointIndex : charOffsets[codePointIndex];
    }

    /**
     * An alias reuses the node of its anchor, so a secret behind an alias is rewritten once, where
     * the anchor is. Secret nodes are tracked apart from visited, so a secret is still found when
     * its anchor sits under a key that is not secret.
     */
    void collect(Node node, List<String> path) throws UnparsableException {
      if (!visited.add(node)) {
        return;
      }
      if (node instanceof ScalarNode) {
        ScalarNode scalar = (ScalarNode) node;
        if (mentionsSecret(scalar.getValue(), secretKeys)) {
          addSecret(scalar, path, Kind.TEXT);
        } else {
          addSpan(scalar);
        }
      } else if (node instanceof MappingNode) {
        for (NodeTuple tuple : ((MappingNode) node).getValue()) {
          Node keyNode = tuple.getKeyNode();
          if (!(keyNode instanceof ScalarNode)) {
            collect(keyNode, path);
            continue;
          }
          addSpan((ScalarNode) keyNode);
          String key = ((ScalarNode) keyNode).getValue();
          List<String> childPath = new ArrayList<>(path);
          childPath.add(key);
          Node value = tuple.getValueNode();
          boolean hba = GFlagsUtil.YSQL_HBA_CONF_CSV.equals(key);
          if (hba || secretKeys.contains(key)) {
            if (!(value instanceof ScalarNode)) {
              throw new UnparsableException(key + " is not a scalar");
            }
            visited.add(value);
            addSecret((ScalarNode) value, childPath, hba ? Kind.HBA : Kind.VALUE);
          } else {
            collect(value, childPath);
          }
        }
      } else if (node instanceof SequenceNode) {
        List<Node> items = ((SequenceNode) node).getValue();
        for (int i = 0; i < items.size(); i++) {
          List<String> childPath = new ArrayList<>(path);
          childPath.add(String.valueOf(i));
          collect(items.get(i), childPath);
        }
      }
    }

    void addSecret(ScalarNode scalar, List<String> path, Kind kind) {
      if (secretNodes.add(scalar)) {
        int[] span = addSpan(scalar);
        secrets.add(new Secret(path, kind, scalar, span[0], span[1]));
      }
    }

    /**
     * Records where the scalar's text is, and returns it. A block scalar's header line can end in a
     * comment, so only the lines below it are recorded as scalar text.
     */
    int[] addSpan(ScalarNode scalar) {
      int end = charOffset(scalar.getEndMark().getIndex());
      int start = scalarStart(charOffset(scalar.getStartMark().getIndex()), end);
      boolean block =
          scalar.getScalarStyle() == ScalarStyle.LITERAL
              || scalar.getScalarStyle() == ScalarStyle.FOLDED;
      int headerEnd = block ? text.indexOf('\n', start) : -1;
      scalarSpans.add(
          new int[] {
            headerEnd < 0 || headerEnd >= end ? (block ? end : start) : headerEnd + 1, end
          });
      return new int[] {start, end};
    }

    /**
     * Where the scalar's own text starts. SnakeYAML starts the mark at an &anchor or !tag in front
     * of the scalar, which is kept so aliases still resolve. A scalar cannot start with & or !
     * itself: plain scalars may not, and the other styles start with a quote, | or >. An empty
     * scalar ends at its properties, so the skip stops at the end.
     */
    int scalarStart(int start, int end) {
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

    /**
     * The comments, outside every scalar: from a # at the start of a line or after a blank to the
     * end of the line. A # inside an anchor, tag or alias name follows no blank.
     */
    List<int[]> comments() {
      scalarSpans.sort(Comparator.comparingInt((int[] span) -> span[0]));
      List<int[]> comments = new ArrayList<>();
      int from = 0;
      for (int i = 0; i <= scalarSpans.size(); i++) {
        int to = i < scalarSpans.size() ? scalarSpans.get(i)[0] : text.length();
        for (int at = from; at < to; at++) {
          if (text.charAt(at) == '#' && (at == 0 || Character.isWhitespace(text.charAt(at - 1)))) {
            int lineEnd = at;
            while (lineEnd < to && text.charAt(lineEnd) != '\n' && text.charAt(lineEnd) != '\r') {
              lineEnd++;
            }
            comments.add(new int[] {at, lineEnd});
            at = lineEnd;
          }
        }
        if (i < scalarSpans.size()) {
          from = Math.max(from, scalarSpans.get(i)[1]);
        }
      }
      return comments;
    }
  }

  /**
   * Replaces a scalar, whatever its style, with text that fits on its first line. A block scalar
   * ends after its line breaks, which are kept so the next key stays on a line of its own.
   */
  private static Replacement replaceScalar(String text, Secret secret, String replacement) {
    int contentEnd = secret.end;
    while (contentEnd > secret.start
        && (text.charAt(contentEnd - 1) == '\n' || text.charAt(contentEnd - 1) == '\r')) {
      contentEnd--;
    }
    return new Replacement(
        secret.start, secret.end, replacement + text.substring(contentEnd, secret.end));
  }

  /**
   * The stored scalar as the user wrote it, when it sits on one line; a multi-line scalar depends
   * on its indentation, so it is quoted instead.
   */
  private static String storedScalarText(String stored, Secret secret) {
    String text = stored.substring(secret.start, secret.end);
    return text.indexOf('\n') < 0 && text.indexOf('\r') < 0 ? text : quote(secret.value.getValue());
  }

  /** A JSON string is a YAML double-quoted scalar. */
  private static String quote(String value) {
    try {
      return Json.mapper().writeValueAsString(value);
    } catch (JsonProcessingException e) {
      throw new IllegalStateException(e);
    }
  }

  /** Whether at falls inside one of the replacements, sorted by start and not overlapping. */
  private static boolean insideAny(List<Replacement> sorted, int at) {
    int lo = 0;
    int hi = sorted.size() - 1;
    while (lo <= hi) {
      int mid = (lo + hi) >>> 1;
      Replacement r = sorted.get(mid);
      if (at < r.start) {
        hi = mid - 1;
      } else if (at >= r.end) {
        lo = mid + 1;
      } else {
        return true;
      }
    }
    return false;
  }

  /** Replacements do not overlap: each is a distinct secret node's span or a comment. */
  private static String apply(String text, List<Replacement> replacements) {
    if (replacements.isEmpty()) {
      return text;
    }
    replacements.sort(Comparator.comparingInt((Replacement r) -> r.start));
    StringBuilder out = new StringBuilder(text.length());
    int from = 0;
    for (Replacement r : replacements) {
      out.append(text, from, r.start).append(r.text);
      from = r.end;
    }
    return out.append(text, from, text.length()).toString();
  }
}
