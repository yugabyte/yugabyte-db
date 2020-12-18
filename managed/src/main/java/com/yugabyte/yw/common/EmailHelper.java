// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.UUID;
import java.util.Map.Entry;

import javax.inject.Inject;
import javax.mail.Authenticator;
import javax.mail.Message;
import javax.mail.MessagingException;
import javax.mail.Multipart;
import javax.mail.PasswordAuthentication;
import javax.mail.Session;
import javax.mail.Transport;
import javax.mail.internet.InternetAddress;
import javax.mail.internet.MimeBodyPart;
import javax.mail.internet.MimeMessage;
import javax.mail.internet.MimeMultipart;

import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.forms.CustomerRegisterFormData.SmtpData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;

import play.libs.Json;

@Singleton
public class EmailHelper {

  public static final Logger LOG = LoggerFactory.getLogger(EmailHelper.class);

  @Inject
  private RuntimeConfigFactory configFactory;

  public static final String DEFAULT_SMTP_SERVER = "email-smtp.us-west-2.amazonaws.com";

  public static final int DEFAULT_SMTP_PORT = 25;

  public static final int DEFAULT_SMTP_PORT_SSL = 587;

  /**
   * Sends email with subject and content to recipients from destinations. STMP
   * parameters are in {@link smtpData}.
   * <p>
   * The content map can hold more than one part. To save the parts order use the
   * appropriate Map implementation (as example, LinkedHashMap).
   * <p>
   * If smtpData.smtpServer is empty then the default SMTP server is used -
   * <i>email-smtp.us-west-2.amazonaws.com</i>
   * <p>
   * If smtpData.smtpPort is not set/filled (equals to -1), used a value for the
   * default SMTP port: 25 for non SSL connection, 587 - for SSL.
   *
   * @param subject      email subject
   * @param destinations list of recipients comma separated
   * @param smtpData     SMTP configuration parameters
   * @param content      map of email body parts; key - content type, value - text
   *
   * @throws MessagingException
   */
  public void sendEmail(String subject, String destinations,
      CustomerRegisterFormData.SmtpData smtpData, Map<String, String> content)
      throws MessagingException {
    LOG.info("Sending email: '{}' to '{}'", subject, destinations);

    Properties prop = new Properties();
    try {
      prop.put("mail.smtp.user", smtpData.smtpUsername);
      prop.put("mail.smtp.auth", true);
      prop.put("mail.smtp.starttls.enable", Boolean.valueOf(smtpData.useTLS));
      prop.put("mail.smtp.host",
          StringUtils.isEmpty(smtpData.smtpServer) ? DEFAULT_SMTP_SERVER : smtpData.smtpServer);
      prop.put("mail.smtp.port",
          String.valueOf(smtpData.smtpPort == -1
              ? (smtpData.useSSL ? DEFAULT_SMTP_PORT_SSL : DEFAULT_SMTP_PORT)
              : smtpData.smtpPort));
      prop.put("mail.smtp.ssl.enable", Boolean.valueOf(smtpData.useSSL));
      if (smtpData.useSSL) {
        prop.put("mail.smtp.ssl.trust", smtpData.smtpServer);
      }
    } catch (Exception e) {
      e.printStackTrace();
      throw new IllegalArgumentException("SmtpData is not correctly filled.", e);
    }

    if (StringUtils.isEmpty(smtpData.emailFrom)) {
      throw new IllegalArgumentException(
          "SmtpData is not correctly filled: emailFrom can't be empty.");
    }

    Session session = Session.getInstance(prop, new Authenticator() {
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
   * Returns default YB email address specified in the configuration file as a
   * parameter with name <i><b>yb.health.default_email</b></i>.
   *
   * @param customer
   * @return
   */
  private String getYbEmail(Customer customer) {
    return configFactory.forCustomer(customer).getString("yb.health.default_email");
  }

  /**
   * Returns a list of email destinations configured for the specified customer.
   * If the customer has flag sendAlertsToYb set then default YB address is added
   * (see {@link #getYbEmail}).
   *
   * @param customerUUID
   * @return
   */
  public List<String> getDestinations(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    List<String> destinations = new ArrayList<>();
    String ybEmail = getYbEmail(customer);
    CustomerConfig config = CustomerConfig.getAlertConfig(customer.uuid);
    CustomerRegisterFormData.AlertingData alertingData = Json.fromJson(config.data,
        CustomerRegisterFormData.AlertingData.class);
    if (alertingData.sendAlertsToYb && !StringUtils.isEmpty(ybEmail)) {
      destinations.add(ybEmail);
    }

    if (!StringUtils.isEmpty(alertingData.alertingEmail)) {
      destinations.add(alertingData.alertingEmail);
    }
    return destinations;
  }

  /**
   * Returns the {@link SmtpData} instance fulfilled with parameters of the
   * specified customer.
   * <p>
   * If the the Smtp configuration doesn't exist for the customer, the default
   * Smtp configuration is created with the next data:
   * <p>
   * <ul>
   * <li>stmpUsername is taken from the configuration file, parameter
   * <i><b>yb.health.ses_email_username</b></i>;</li>
   * <li>smtpPassword is taken from the configuration file, parameter
   * <i><b>yb.health.ses_email_password</b></i>;</li>
   * <li>useSSL is taken from the configuration file, parameter
   * <i><b>yb.health.default_ssl</b></i>, by default is <b>true</b>.</li>
   * </ul>
   * <p>
   * Also if emailFrom is empty (for both cases) it is filled with the default YB
   * address (see {@link #getYbEmail})
   *
   * @param customerUUID
   * @return
   */
  public SmtpData getSmtpData(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    CustomerConfig smtpConfig = CustomerConfig.getSmtpConfig(customerUUID);
    SmtpData smtpData;
    if (smtpConfig != null) {
      smtpData = Json.fromJson(smtpConfig.data, CustomerRegisterFormData.SmtpData.class);
    } else {
      smtpData = new SmtpData();
      smtpData.smtpUsername = configFactory.forCustomer(customer)
          .getString("yb.health.ses_email_username");
      smtpData.smtpPassword = configFactory.forCustomer(customer)
          .getString("yb.health.ses_email_password");
      smtpData.useSSL = configFactory.forCustomer(customer).getBoolean("yb.health.default_ssl");
      smtpData.useTLS = configFactory.forCustomer(customer).getBoolean("yb.health.default_tls");
    }

    if (StringUtils.isEmpty(smtpData.emailFrom)) {
      smtpData.emailFrom = getYbEmail(customer);
    }
    return smtpData;
  }
}
