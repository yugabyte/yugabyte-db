package com.yugabyte.configencrypt;

import com.beust.jcommander.JCommander;
import com.beust.jcommander.Parameter;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.security.crypto.encrypt.Encryptors;
import org.springframework.security.crypto.encrypt.TextEncryptor;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

/**
 * Hello world!
 *
 */
public class ConfigEncrypt {
  @Parameter(names = {"-c", "--customer-uuid"}, required = true,
      description = "customer uuid that created the provider")
  private static String customerUUID;

  @Parameter(names = "--code", description = "Provider code for the config we are working with")
  private static String code = "gcp";

  @Parameter(names = {"-e", "--encrypted-data"},
      description = "data to decrypt - '80bda43bc9d241f3'")
  private static String encryptedData;

  @Parameter(names = {"-j", "-json-data"},
      description = "json data to encrypt - '{\"key\": \"value\"}'")
  private static String jsonData;

  @Parameter(names = "--help", help = true)
  private static boolean help;

    public static void main( String[] args ) throws Exception {
        ConfigEncrypt configencrypt = new ConfigEncrypt();
        JCommander jc = JCommander.newBuilder()
          .addObject(configencrypt)
          .build();
        jc.parse(args);
        if (help){
          jc.usage();
          return;
        }
        configencrypt.run();
    }

    public void run() throws Exception {
        if (jsonData == null && encryptedData == null){
          throw new Exception("one of json data or encrypted data must be provided");
        }
        final UUID cUUID = UUID.fromString(customerUUID);

        final ObjectMapper mapper = new ObjectMapper();
        if (jsonData != null ) {
          Map<String, String> config = mapper.readValue(jsonData, new TypeReference<Map<String, String>>() {});
          config = encryptProviderConfig(config, cUUID, code);
          System.out.println(mapper.writeValueAsString(config));
        }
        if (encryptedData != null ) {
          Map<String, String> config = decryptProviderConfig(encryptedData, cUUID, code);
          System.out.println(mapper.writeValueAsString(config));
        }
    }

    public static Map<String, String> encryptProviderConfig(
      Map<String, String> config, UUID customerUUID, String providerCode) {
        if (config.isEmpty()) return new HashMap<>();
        try {
            final ObjectMapper mapper = new ObjectMapper();
            final String salt = generateSalt(customerUUID, providerCode);
            final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
            final String encryptedConfig = encryptor.encrypt(mapper.writeValueAsString(config));
            Map<String, String> encryptMap = new HashMap<>();
            encryptMap.put("encrypted", encryptedConfig);
            return encryptMap;
        } catch (Exception e) {
            final String errMsg =
                String.format(
                    "Could not encrypt provider configuration for customer %s", customerUUID.toString());
            System.out.println(errMsg);
            return null;
        }
    }

  public static Map<String, String> decryptProviderConfig(
    String encryptedConfig, UUID customerUUID, String providerCode) throws Exception {
    final ObjectMapper mapper = new ObjectMapper();
    final String salt = generateSalt(customerUUID, providerCode);
    final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
    final String decryptedConfig = encryptor.decrypt(encryptedConfig);
    return mapper.readValue(decryptedConfig, new TypeReference<Map<String, String>>() {});
  }

  public static String generateSalt(UUID customerUUID, String providerCode) {
    final String kpValue = String.valueOf(providerCode.hashCode());
    final String saltBase = "%s%s";
    final String salt =
        String.format(saltBase, customerUUID.toString().replace("-", ""), kpValue.replace("-", ""));
    return salt.length() % 2 == 0 ? salt : salt + "0";
  }
}
