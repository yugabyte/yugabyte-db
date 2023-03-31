// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.AlertingData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Properties;
import java.util.UUID;
import javax.inject.Inject;
import javax.mail.Authenticator;
import javax.mail.Message;
import javax.mail.MessagingException;
import javax.mail.Multipart;
import javax.mail.PasswordAuthentication;
import javax.mail.Session;
import javax.mail.Transport;
import javax.mail.internet.AddressException;
import javax.mail.internet.InternetAddress;
import javax.mail.internet.MimeBodyPart;
import javax.mail.internet.MimeMessage;
import javax.mail.internet.MimeMultipart;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@Singleton
public class EmailHelper {

  public static final Logger LOG = LoggerFactory.getLogger(EmailHelper.class);

  public static final String DEFAULT_EMAIL_SEPARATORS = ";,";

  @Inject private RuntimeConfigFactory configFactory;

  /**
   * Sends email with subject and content to recipients from destinations. STMP parameters are in
   * {@link smtpData}.
   *
   * <p>The content map can hold more than one part. To save the parts order use the appropriate Map
   * implementation (as example, LinkedHashMap).
   *
   * <p>If smtpData.smtpServer is empty, used configuration value "yb.health.default_smtp_server".
   *
   * <p>If smtpData.smtpPort is not set/filled (equals to -1), used configuration value
   * "yb.health.default_smtp_port" for non SSL connection, "yb.health.default_smtp_port_ssl" - for
   * SSL.
   *
   * @param customer customer instance (used to get runtime configuration values)
   * @param subject email subject
   * @param destinations list of recipients comma separated
   * @param smtpData SMTP configuration parameters
   * @param content map of email body parts; key - content type, value - text
   * @throws MessagingException
   */
  public void sendEmail(
      Customer customer,
      String subject,
      String destinations,
      SmtpData smtpData,
      Map<String, String> content)
      throws MessagingException {
    LOG.info("Sending email: '{}'", subject);

    Session session =
        Session.getInstance(
            smtpDataToProperties(customer, smtpData),
            new Authenticator() {
              @Override
              protected PasswordAuthentication getPasswordAuthentication() {
                return new PasswordAuthentication(smtpData.smtpUsername, smtpData.smtpPassword);
              }
            });

    Message message = new MimeMessage(session);
    message.setFrom(new InternetAddress(smtpData.emailFrom));
    // message.setReplyTo(smtpData.emailFrom); -- ?
    message.setRecipients(Message.RecipientType.TO, InternetAddress.parse(destinations));
    message.setSubject(subject);

    Multipart multipart = new MimeMultipart("alternative");
    for (Entry<String, String> entry : content.entrySet()) {
      MimeBodyPart mimeBodyPart = new MimeBodyPart();
      mimeBodyPart.setContent(entry.getValue(), entry.getKey());
      multipart.addBodyPart(mimeBodyPart);
    }
    message.setContent(multipart);

    Transport.send(message);
  }

  /**
   * Converts smtpData into properties used for mail session.
   *
   * @param smtpData
   * @return
   * @throws IllegalArgumentException if some parameters are not filled/incorrect.
   */
  @VisibleForTesting
  Properties smtpDataToProperties(Customer customer, SmtpData smtpData) {
    Properties props = new Properties();
    try {
      Config runtimeConfig = configFactory.forCustomer(customer);

      // According to official Java documentation all the parameters should be added
      // as String.
      if (smtpData.smtpUsername != null) {
        props.put("mail.smtp.auth", "true");
        props.put("mail.smtp.user", smtpData.smtpUsername);
      } else {
        props.put("mail.smtp.auth", "false");
      }
      props.put("mail.smtp.starttls.enable", String.valueOf(smtpData.useTLS));
      props.put("mail.smtp.ssl.protocols", "TLSv1.3 TLSv1.2 TLSv1.1 TLSv1");
      String smtpServer =
          StringUtils.isEmpty(smtpData.smtpServer)
              ? runtimeConfig.getString("yb.health.default_smtp_server")
              : smtpData.smtpServer;
      props.put("mail.smtp.host", smtpServer);
      props.put(
          "mail.smtp.port",
          String.valueOf(
              smtpData.smtpPort == -1
                  ? (smtpData.useSSL
                      ? runtimeConfig.getInt("yb.health.default_smtp_port_ssl")
                      : runtimeConfig.getInt("yb.health.default_smtp_port"))
                  : smtpData.smtpPort));
      props.put("mail.smtp.ssl.enable", String.valueOf(smtpData.useSSL));
      if (smtpData.useSSL) {
        props.put("mail.smtp.ssl.trust", smtpServer);
      }

      boolean isDebugMode = runtimeConfig.getBoolean("yb.health.debug_email");
      if (isDebugMode) {
        props.put("mail.debug", "true");
      }

      // Adding timeout settings.
      String connectionTimeout =
          String.valueOf(runtimeConfig.getInt("yb.health.smtp_connection_timeout_ms"));
      props.put(
          smtpData.useSSL ? "mail.smtps.connectiontimeout" : "mail.smtp.connectiontimeout",
          connectionTimeout);

      String timeout = String.valueOf(runtimeConfig.getInt("yb.health.smtp_timeout_ms"));
      props.put(smtpData.useSSL ? "mail.smtps.timeout" : "mail.smtp.timeout", timeout);

      if (isDebugMode) {
        LOG.info("SMTP connection timeout: " + connectionTimeout);
        LOG.info("SMTP timeout: " + timeout);
      }

    } catch (Exception e) {
      LOG.error("Error while converting smtpData to Properties", e);
      throw new IllegalArgumentException("SmtpData is not correctly filled.", e);
    }

    if (StringUtils.isEmpty(smtpData.emailFrom)) {
      throw new IllegalArgumentException(
          "SmtpData is not correctly filled: emailFrom can't be empty.");
    }
    return props;
  }

  /**
   * Returns default YB email address specified in the configuration file as a parameter with name
   * <i><b>yb.health.default_email</b></i>.
   *
   * @param customer
   * @return
   */
  private String getYbEmail(Customer customer) {
    return configFactory.forCustomer(customer).getString("yb.health.default_email");
  }

  /**
   * Returns a list of email destinations configured for the specified customer. If the customer has
   * flag sendAlertsToYb set then default YB address is added (see {@link #getYbEmail}).
   *
   * @param customerUUID
   * @return
   */
  public List<String> getDestinations(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    List<String> destinations = new ArrayList<>();
    String ybEmail = getYbEmail(customer);
    CustomerConfig config = CustomerConfig.getAlertConfig(customer.getUuid());
    if (config != null) {
      AlertingData alertingData = Json.fromJson(config.getData(), AlertingData.class);
      if (alertingData.sendAlertsToYb && !StringUtils.isEmpty(ybEmail)) {
        destinations.add(ybEmail);
      }

      if (!StringUtils.isEmpty(alertingData.alertingEmail)) {
        destinations.add(alertingData.alertingEmail);
      }
    }
    return destinations;
  }

  // TODO: (Sergey Potachev) Extract SmtpData class from CustomerRegisterFormData.
  // Move this logic to SmtpData (together with smtpDataToProperties).
  /**
   * Returns the {@link SmtpData} instance fulfilled with parameters of the specified customer.
   *
   * <p>Also if emailFrom is empty it is filled with the default YB address (see {@link
   * #getYbEmail})
   *
   * @param customerUUID
   * @return filled SmtpData if all parameters exist or NULL otherwise
   */
  public SmtpData getSmtpData(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    CustomerConfig smtpConfig = CustomerConfig.getSmtpConfig(customerUUID);

    if (smtpConfig == null) {
      return null;
    }
    SmtpData smtpData = Json.fromJson(smtpConfig.getData(), SmtpData.class);
    if (StringUtils.isEmpty(smtpData.emailFrom)) {
      smtpData.emailFrom = getYbEmail(customer);
    }
    return StringUtils.isEmpty(smtpData.emailFrom) ? null : smtpData;
  }

  /**
   * Splits string with emails addresses using the passed <tt>separators</tt>. Blocks enclosed with
   * quotes are not splitted.
   *
   * @param emails
   * @param separators
   * @return
   */
  public static List<String> splitEmails(String emails, String separators) {
    List<String> result = new ArrayList<>();
    int startPosition = 0;
    int currPosition = 0;
    while (currPosition < emails.length()) {
      char c = emails.charAt(currPosition);
      if (c == '"') {
        int closingQuotesPos = emails.indexOf('"', currPosition + 1);
        currPosition = closingQuotesPos == -1 ? emails.length() - 1 : closingQuotesPos;
      } else if (separators.indexOf(c) >= 0) {
        String email = emails.substring(startPosition, currPosition).trim();
        if (email.length() > 0) {
          result.add(email);
          startPosition = currPosition + 1;
        }
      }
      currPosition++;
    }
    // Copying tail of the string if needed.
    if (startPosition < currPosition) {
      String email = emails.substring(startPosition, currPosition).trim();
      if (email.length() > 0) {
        result.add(email);
      }
    }
    return result;
  }

  /**
   * Extracts pure email address from the common email string (which can be like "John Doe"
   * <john@google.com>). Doesn't validate the email correctness.
   *
   * @param email
   * @return Extracted email or null if the address can't be extracted or is incorrect.
   */
  public static String extractEmailAddress(String email) {
    try {
      InternetAddress addr = new InternetAddress(email, false);
      return addr.getAddress();
    } catch (AddressException e) {
      return null;
    }
  }
}
