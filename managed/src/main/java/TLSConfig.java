// Copyright (c) YugaByte, Inc.
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import java.security.Security;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

@Singleton
public class TLSConfig {
  /*
   * Helper class for modifying the TLS config property for the java application.
   */

  public static String tlsDisabledKey = "jdk.tls.disabledAlgorithms";

  public static String getTLSDisabledAlgorithms() {
    return Security.getProperty(tlsDisabledKey);
  }

  public static void setTLSDisabledAlgorithms(String algorithms) {
    Security.setProperty(tlsDisabledKey, algorithms);
  }

  public static void modifyTLSDisabledAlgorithms(Config config) {
    String algorithms = TLSConfig.getTLSDisabledAlgorithms();
    List<String> algorithmList = new ArrayList<>(Arrays.asList(algorithms.split(", ")));
    // Process existing algorithm to remove the keySize < 2048 from the list
    Pattern pattern = Pattern.compile("(\\w+\\s+keySize)\\s*<\\s*(\\d+)");
    for (int i = 0; i < algorithmList.size(); i++) {
      String algorithm = algorithmList.get(i);
      Matcher matcher = pattern.matcher(algorithm);
      if (matcher.find()) {
        int keySize = Integer.parseInt(matcher.group(2));
        if (keySize % 256 == 0 && keySize < 2048) {
          // Replace the entry with keySize < 2048
          algorithmList.set(i, matcher.group(1) + " < 2048");
        }
      }
    }

    if (config.hasPath("yb.security.java.tls.disabled_algorithms")) {
      List<String> toDisableAlgorithms =
          config.getStringList("yb.security.java.tls.disabled_algorithms").stream()
              .collect(Collectors.toList());
      if (toDisableAlgorithms != null && toDisableAlgorithms.size() > 0) {
        algorithmList.addAll(toDisableAlgorithms);
      }

      algorithms = String.join(", ", algorithmList);
      TLSConfig.setTLSDisabledAlgorithms(algorithms);
    }
  }
}
