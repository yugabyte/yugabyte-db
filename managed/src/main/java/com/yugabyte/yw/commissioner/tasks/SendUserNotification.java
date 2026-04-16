package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import jakarta.mail.MessagingException;
import java.time.Duration;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SendUserNotification extends AbstractTaskBase {

  private final EmailHelper emailHelper;
  private static final int MAX_RETRY_ATTEMPTS = 3;
  private final Duration SLEEP_TIME = Duration.ofSeconds(10);

  @Inject
  public SendUserNotification(BaseTaskDependencies baseTaskDependencies, EmailHelper emailHelper) {
    super(baseTaskDependencies);
    this.emailHelper = emailHelper;
  }

  public static class Params extends AbstractTaskParams {
    public UUID customerUUID;
    public UUID userUUID;
    public String emailSubject;
    public String emailBody;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Customer customer = Customer.getOrBadRequest(params().customerUUID);
    Users user = Users.getOrBadRequest(params().userUUID);
    String emailDestinations = user.getEmail();
    if (emailDestinations == null || emailDestinations.isEmpty()) {
      log.warn("Email destination is empty or null");
      return;
    }
    if (!sendNotificationToUser(customer, emailDestinations)) {
      log.warn(
          "Failed to send email after {} attempts to {}. Skipping",
          MAX_RETRY_ATTEMPTS,
          emailDestinations);
    } else {
      log.info("Password reset notification sent to user: " + emailDestinations);
    }
  }

  private boolean sendNotificationToUser(Customer customer, String emailDestinations) {

    SmtpData smtpData = emailHelper.getSmtpData(customer.getUuid());
    if (smtpData == null) {
      log.warn("SMTP data not found for customer: {}", customer.getUuid());
      return false;
    }
    Map<String, String> contentMap = Map.of("text/plain; charset=\"us-ascii\"", params().emailBody);
    for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS; attempt++) {
      try {
        emailHelper.sendEmail(
            customer, params().emailSubject, emailDestinations, smtpData, contentMap);
        log.info("Email sent successfully on attempt {}", attempt);
        return true;
      } catch (MessagingException e) {
        log.warn("Attempt {} to send email failed: {}", attempt, e.getMessage());
        if (attempt == MAX_RETRY_ATTEMPTS) {
          log.warn("All attempts to send email failed for {}", emailDestinations);
        } else {
          waitFor(SLEEP_TIME);
        }
      }
    }
    return false;
  }
}
