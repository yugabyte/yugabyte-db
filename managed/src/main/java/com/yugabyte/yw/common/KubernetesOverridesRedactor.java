// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.core.JsonProcessingException;
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
import org.yaml.snakeyaml.DumperOptions.FlowStyle;
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
 * Redacts the LDAP secrets in Kubernetes overrides YAML for API responses, and puts the stored
 * values back when overrides copied from a response are submitted. Only the secrets are rewritten,
 * located by parsing the YAML, so the rest of the user's text is kept: the UI shows it as typed,
 * and overrides sent back unchanged equal redact(stored) exactly. A secret is the value of a secret
 * gflag, the ldapbindpasswd fields of ysql_hba_conf_csv, any other string that mentions a secret,
 * and a comment that does; strings and comments are redacted as the logs redact text.
 *
 * <p>Overrides that hold a secret are hidden whole as REDACTED instead when they can't be parsed,
 * or when they use YAML the rewrite doesn't follow: anchors, aliases and tags, which can carry a
 * value to another key or change how it loads, keys that aren't strings, a repeated secret key, and
 * a key that mentions a secret. Overrides rarely use these, and hiding them whole fails closed.
 * Sent back unchanged, hidden overrides restore the stored text; an edit has to submit them in
 * full.
 */
@Slf4j
public final class KubernetesOverridesRedactor {

  private static final String REDACTED = RedactingService.SECRET_REPLACEMENT;

  private KubernetesOverridesRedactor() {}

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

    // Contains, not equals, as RedactingService.isGFlagRedacted: "REDACTED " or a block scalar
    // holding REDACTED must not be stored as the token.
    boolean holdsRedacted(String value) {
      return value.contains(REDACTED);
    }
  }

  private static class Secret {
    final List<String> path;
    final Kind kind;
    final ScalarNode value;
    // The scalar's own text in the overrides, as char offsets, and whether it sits inside a flow
    // collection ({...} or [...]), where a plain scalar can't hold a comma or brackets.
    final int start;
    final int end;
    final boolean inFlow;
    // Under a list item, whose index changes when items are added, removed or reordered.
    final boolean inList;

    Secret(
        List<String> path,
        Kind kind,
        ScalarNode value,
        int start,
        int end,
        boolean inFlow,
        boolean inList) {
      this.path = path;
      this.kind = kind;
      this.value = value;
      this.start = start;
      this.end = end;
      this.inFlow = inFlow;
      this.inList = inList;
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
   * Thrown for overrides that are hidden whole. The message never quotes the text, which may hold a
   * secret.
   */
  private static class HiddenException extends Exception {
    // False when the text can't be parsed at all.
    final boolean parsed;

    HiddenException(String message, boolean parsed) {
      super(message);
      this.parsed = parsed;
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
    if (overrides == null) {
      return null;
    }
    boolean mentionsSecret = mentionsSecret(overrides, secretKeys());
    // A double-quoted key can spell a secret name with escapes, so text with a backslash is parsed.
    if (!mentionsSecret && overrides.indexOf('\\') < 0) {
      return overrides;
    }
    Finder found;
    try {
      found = find(overrides);
    } catch (HiddenException e) {
      // Escapes spell a name only once parsed, so text that can't be parsed holds a secret only if
      // it names one as written, as above.
      if (!e.parsed && !mentionsSecret) {
        return overrides;
      }
      log.debug("Hiding Kubernetes overrides whole because they {}", e.getMessage());
      return REDACTED;
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
   * the rest of the value differs from the stored one. Also a 400 for REDACTED in overrides that
   * are hidden whole or under a list item, which can't be matched to a stored value. Comments are
   * not restored. stored is null for new overrides. Overrides sent back unchanged, as stored or as
   * redacted, are returned as stored, even when the stored text already holds REDACTED.
   */
  public static String restore(String stored, String submitted, String where) {
    if (submitted == null || !submitted.contains(REDACTED)) {
      return submitted;
    }
    // Unchanged overrides, as stored or as redacted, change nothing, even if the stored text
    // already holds REDACTED: edits and upgrades send the universe's own overrides back, and a 400
    // there would block every operation on that universe.
    if (stored != null && (submitted.equals(stored) || submitted.equals(redact(stored)))) {
      return stored;
    }
    Finder found;
    try {
      found = find(submitted);
    } catch (HiddenException e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s contain %s, but the stored values cannot be restored into them: they %s."
                  + " Submit the actual values.",
              where, REDACTED, e.getMessage()));
    }
    if (found.root instanceof ScalarNode
        && ((ScalarNode) found.root).getValue().contains(REDACTED)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s are %s, as overrides hidden whole are shown. To change them, submit them in"
                  + " full.",
              where, REDACTED));
    }
    Map<List<String>, Secret> storedSecrets = new HashMap<>();
    String storedProblem = null;
    if (stored != null) {
      try {
        storedSecrets.putAll(find(stored).byPath);
      } catch (HiddenException e) {
        storedProblem = e.getMessage();
      }
    }
    List<Replacement> replacements = new ArrayList<>();
    for (Secret secret : found.secrets) {
      String value = secret.value.getValue();
      if (!secret.kind.holdsRedacted(value)) {
        continue;
      }
      String name = where + " at " + String.join(".", secret.path);
      if (secret.inList) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s contain %s under a list item, which can't be matched to a stored value."
                    + " Submit the actual value.",
                name, REDACTED));
      }
      Secret storedSecret = storedSecrets.get(secret.path);
      if (storedSecret == null && storedProblem != null) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s contain %s, and the stored overrides cannot be read to restore it from: they"
                    + " %s. Submit the actual value.",
                name, REDACTED, storedProblem));
      }
      if (storedSecret == null) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s contain %s, and nothing is stored at the same place to restore it from."
                    + " Submit the actual value.",
                name, REDACTED));
      }
      String storedValue = storedSecret.value.getValue();
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
      replacements.add(
          replaceScalar(submitted, secret, storedScalarText(stored, storedSecret, secret.inFlow)));
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

  private static Finder find(String overrides) throws HiddenException {
    LoaderOptions options = new LoaderOptions();
    if (overrides.codePointCount(0, overrides.length()) > options.getCodePointLimit()) {
      throw new HiddenException(
          "are longer than " + options.getCodePointLimit() + " code points", false);
    }
    Node root;
    try {
      root = new Yaml(new SafeConstructor(options)).compose(new StringReader(overrides));
    } catch (MarkedYAMLException e) {
      // Its message quotes the line around the problem, which may hold a secret.
      Mark mark = e.getProblemMark();
      throw new HiddenException(
          "cannot be parsed: "
              + (e.getContext() == null ? "" : e.getContext() + ": ")
              + e.getProblem()
              + (mark == null
                  ? ""
                  : " at line " + (mark.getLine() + 1) + ", column " + (mark.getColumn() + 1)),
          false);
    } catch (RuntimeException e) {
      throw new HiddenException("cannot be parsed: " + e.getClass().getSimpleName(), false);
    }
    Finder finder = new Finder(overrides, root);
    if (root != null) {
      finder.collect(root, new ArrayList<>(), false, false);
    }
    if (finder.unsupported != null
        && (!finder.secrets.isEmpty() || mentionsSecret(overrides, finder.secretKeys))) {
      throw new HiddenException(finder.unsupported, true);
    }
    return finder;
  }

  private static class Finder {
    final String text;
    final Node root;
    // SnakeYAML marks count code points: the char offset of each, or null when they are the same.
    final int[] charOffsets;
    final Set<String> secretKeys = secretKeys();
    final List<Secret> secrets = new ArrayList<>();
    final Map<List<String>, Secret> byPath = new HashMap<>();
    final Set<Node> visited = new HashSet<>();
    // Every scalar's text, which no comment can be inside of.
    final List<int[]> scalarSpans = new ArrayList<>();
    // YAML the rewrite doesn't follow, which hides the overrides whole if they hold a secret.
    String unsupported;

    Finder(String text, Node root) {
      this.text = text;
      this.root = root;
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
     * Notes an anchor, a tag, and an alias, which reuses the node of its anchor; returns false for
     * a node seen before. SnakeYAML starts a node's mark at its properties, and a node can't start
     * with ! otherwise: a plain scalar may not, and the other styles start with a quote, |, >, [ or
     * {.
     */
    boolean visit(Node node) {
      boolean first = visited.add(node);
      int start = charOffset(node.getStartMark().getIndex());
      if (!first
          || node.getAnchor() != null
          || (start < text.length() && text.charAt(start) == '!')) {
        unsupported = "use an anchor, alias or tag";
      }
      return first;
    }

    void collect(Node node, List<String> path, boolean inFlow, boolean inList)
        throws HiddenException {
      if (!visit(node)) {
        return;
      }
      if (node instanceof ScalarNode) {
        ScalarNode scalar = (ScalarNode) node;
        if (mentionsSecret(scalar.getValue(), secretKeys)) {
          addSecret(scalar, path, Kind.TEXT, inFlow, inList);
        } else {
          addSpan(scalar);
        }
      } else if (node instanceof MappingNode) {
        boolean childInFlow = inFlow || ((MappingNode) node).getFlowStyle() == FlowStyle.FLOW;
        for (NodeTuple tuple : ((MappingNode) node).getValue()) {
          Node keyNode = tuple.getKeyNode();
          Node value = tuple.getValueNode();
          if (!(keyNode instanceof ScalarNode)) {
            unsupported = "have a key that is not a string";
            collect(keyNode, path, childInFlow, inList);
            collect(value, path, childInFlow, inList);
            continue;
          }
          visit(keyNode);
          addSpan((ScalarNode) keyNode);
          String key = ((ScalarNode) keyNode).getValue();
          List<String> childPath = new ArrayList<>(path);
          childPath.add(key);
          boolean hba = GFlagsUtil.YSQL_HBA_CONF_CSV.equals(key);
          if (hba || secretKeys.contains(key)) {
            if (!(value instanceof ScalarNode)) {
              throw new HiddenException("hold a value of " + key + " that is not a scalar", true);
            }
            visit(value);
            addSecret(
                (ScalarNode) value, childPath, hba ? Kind.HBA : Kind.VALUE, childInFlow, inList);
          } else if (mentionsSecret(key, secretKeys)) {
            throw new HiddenException("have a key that mentions a secret", true);
          } else {
            collect(value, childPath, childInFlow, inList);
          }
        }
      } else if (node instanceof SequenceNode) {
        boolean childInFlow = inFlow || ((SequenceNode) node).getFlowStyle() == FlowStyle.FLOW;
        List<Node> items = ((SequenceNode) node).getValue();
        for (int i = 0; i < items.size(); i++) {
          List<String> childPath = new ArrayList<>(path);
          childPath.add(String.valueOf(i));
          collect(items.get(i), childPath, childInFlow, true);
        }
      }
    }

    void addSecret(ScalarNode scalar, List<String> path, Kind kind, boolean inFlow, boolean inList)
        throws HiddenException {
      int[] span = addSpan(scalar);
      Secret secret = new Secret(path, kind, scalar, span[0], span[1], inFlow, inList);
      secrets.add(secret);
      if (byPath.put(path, secret) != null) {
        throw new HiddenException("repeat " + String.join(".", path), true);
      }
    }

    /**
     * Records where the scalar's text is, and returns it. A block scalar's header line can end in a
     * comment, so only the lines below it are recorded as scalar text.
     */
    int[] addSpan(ScalarNode scalar) {
      int start = charOffset(scalar.getStartMark().getIndex());
      int end = charOffset(scalar.getEndMark().getIndex());
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
     * The comments, outside every scalar: from a # at the start of a line or after a blank to the
     * end of the line.
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
   * The stored scalar as the user wrote it, when that reads the same where it goes. Quoted instead
   * when it spans lines, which depend on its indentation, or when it is plain and goes into a flow
   * collection, where a comma or bracket in it would end it.
   */
  private static String storedScalarText(String stored, Secret secret, boolean intoFlow) {
    String text = stored.substring(secret.start, secret.end);
    boolean multiLine = text.indexOf('\n') >= 0 || text.indexOf('\r') >= 0;
    boolean plain = secret.value.getScalarStyle() == ScalarStyle.PLAIN;
    return multiLine || (plain && intoFlow) ? quote(secret.value.getValue()) : text;
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
