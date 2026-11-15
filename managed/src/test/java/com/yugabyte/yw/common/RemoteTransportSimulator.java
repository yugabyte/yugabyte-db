// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import java.util.ArrayList;
import java.util.List;

/**
 * Models what each remote transport actually delivers to the node, so tests can assert that a
 * command survives the trip byte for byte.
 *
 * <p>The models mirror production code that mostly lives outside the Java tree: {@code
 * exec_command} in {@code ybops/utils/ssh.py}, {@code getBashCommand} in {@link NodeAgentClient},
 * and {@code wrap_command} in {@code devops/bin/node_client_utils.py}. Any change to those must be
 * reflected here or these tests stop meaning anything.
 */
public final class RemoteTransportSimulator {

  /** The three ways YBA reaches a DB node. */
  public enum Transport {
    /** Java gRPC to the node agent, which runs argv through exec without a shell. */
    NODE_AGENT,
    /** kubectl exec hands everything after {@code --} to the pod as argv. */
    KUBERNETES,
    /** run_node_action.py, then ssh.py folds argv into one string for the remote login shell. */
    SSH
  }

  private RemoteTransportSimulator() {}

  /**
   * Returns the argv the target program receives once the transport has finished with it. Compare
   * against the argv that was handed to {@code runCommand} to detect corruption.
   */
  public static List<String> deliver(Transport transport, List<String> command, boolean useBash) {
    switch (transport) {
      case NODE_AGENT:
        // useBash=false sends argv verbatim over gRPC. useBash=true first collapses it through
        // getBashCommand into "bash -c <joined>", so the remote bash re-parses the join.
        return useBash ? shellSplit(naiveJoin(command)) : new ArrayList<>(command);
      case KUBERNETES:
        // subprocess is given a list, so no local shell touches it and kubectl forwards argv.
        return new ArrayList<>(command);
      case SSH:
        // ssh.py joins unconditionally, regardless of useBash, and paramiko hands the result to
        // the remote login shell.
        return shellSplit(naiveJoin(command));
      default:
        throw new IllegalArgumentException("Unknown transport " + transport);
    }
  }

  /**
   * Returns the argv the target program receives for a command sent through {@link
   * NodeUniverseManager#runCommand}, including the compensation that method applies before handing
   * the command to ssh.py. This is the entry point tests should use to assert a real caller's
   * command survives.
   */
  public static List<String> deliverViaRunCommand(
      Transport transport, List<String> command, boolean useBash) {
    List<String> sent =
        transport == Transport.SSH
            ? NodeUniverseManager.escapeForSshTransport(command, useBash)
            : command;
    return deliver(transport, sent, useBash);
  }

  /**
   * Returns the parameters a script sees in {@code $@} for a call through {@link
   * NodeUniverseManager#runScript}, including the quoting that method applies for the
   * run_node_action.py transports.
   */
  public static List<String> deliverViaRunScript(Transport transport, List<String> params) {
    return transport == Transport.NODE_AGENT
        ? deliverNodeAgentScriptParams(params)
        : deliverPythonScriptParams(NodeUniverseManager.quoteScriptParams(params));
  }

  /**
   * Returns the parameters a script sees in {@code $@} after the node agent has delivered them.
   * {@code NodeAgentClient.executeScript} quotes each parameter before folding them into the {@code
   * /bin/bash -s ... <<'EOF'} line.
   */
  public static List<String> deliverNodeAgentScriptParams(List<String> params) {
    StringBuilder sb = new StringBuilder();
    for (String param : params) {
      if (sb.length() > 0) {
        sb.append(' ');
      }
      sb.append("'").append(param.replace("'", "'\\''")).append("'");
    }
    return shellSplit(sb.toString());
  }

  /**
   * Returns the parameters a script sees in {@code $@} after run_node_action.py has delivered them.
   * All of {@code exec_script} in ssh.py, {@code check_exec_script} in remote_shell.py and {@code
   * check_exec_script} in node_client_utils.py join the parameters with plain spaces and let the
   * remote shell split the result, so the quoting has to have happened on the Java side.
   */
  public static List<String> deliverPythonScriptParams(List<String> params) {
    return shellSplit(String.join(" ", params));
  }

  /**
   * The join shared by {@code ssh.py exec_command} and {@link NodeAgentClient} {@code
   * getBashCommand}: wrap any element containing a space in single quotes, without escaping single
   * quotes already inside it.
   */
  public static String naiveJoin(List<String> command) {
    StringBuilder sb = new StringBuilder();
    for (String part : command) {
      if (sb.length() > 0) {
        sb.append(' ');
      }
      sb.append(part.contains(" ") ? "'" + part + "'" : part);
    }
    return sb.toString();
  }

  /**
   * Splits a command line the way a POSIX shell would: single quotes are literal, double quotes
   * honour a small set of backslash escapes, and unquoted whitespace separates words.
   *
   * @throws IllegalStateException if a quote is left open, which is how a real shell would fail
   */
  public static List<String> shellSplit(String line) {
    List<String> words = new ArrayList<>();
    StringBuilder current = new StringBuilder();
    boolean inWord = false;
    int i = 0;
    while (i < line.length()) {
      char c = line.charAt(i);
      if (c == ' ' || c == '\t' || c == '\n') {
        if (inWord) {
          words.add(current.toString());
          current.setLength(0);
          inWord = false;
        }
        i++;
      } else if (c == '\'') {
        inWord = true;
        i++;
        int close = line.indexOf('\'', i);
        if (close < 0) {
          throw new IllegalStateException("Unterminated single quote in: " + line);
        }
        current.append(line, i, close);
        i = close + 1;
      } else if (c == '"') {
        inWord = true;
        i++;
        i = appendDoubleQuoted(line, i, current);
      } else if (c == '\\' && i + 1 < line.length()) {
        inWord = true;
        current.append(line.charAt(i + 1));
        i += 2;
      } else {
        inWord = true;
        current.append(c);
        i++;
      }
    }
    if (inWord) {
      words.add(current.toString());
    }
    return words;
  }

  private static int appendDoubleQuoted(String line, int start, StringBuilder current) {
    int i = start;
    while (i < line.length() && line.charAt(i) != '"') {
      char c = line.charAt(i);
      if (c == '\\' && i + 1 < line.length() && isDoubleQuoteEscapable(line.charAt(i + 1))) {
        current.append(line.charAt(i + 1));
        i += 2;
        continue;
      }
      current.append(c);
      i++;
    }
    if (i >= line.length()) {
      throw new IllegalStateException("Unterminated double quote in: " + line);
    }
    return i + 1;
  }

  private static boolean isDoubleQuoteEscapable(char c) {
    return c == '"' || c == '\\' || c == '$' || c == '`' || c == '\n';
  }
}
