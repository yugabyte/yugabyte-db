package openapitools;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.text.SimpleDateFormat;
import java.util.*;
import javax.validation.ConstraintViolation;
import javax.validation.Validation;
import javax.validation.Validator;
import javax.validation.ValidatorFactory;
import play.mvc.With;

public class OpenAPIUtils {

  @With(ApiCall.class)
  @Target({ElementType.TYPE, ElementType.METHOD})
  @Retention(RetentionPolicy.RUNTIME)
  public @interface ApiAction {}

  public static <T> void validate(T obj) {
    ValidatorFactory factory = Validation.buildDefaultValidatorFactory();
    Validator validator = factory.getValidator();
    Set<ConstraintViolation<T>> constraintViolations = validator.validate(obj);
    if (constraintViolations.size() > 0) {
      StringBuilder errors = new StringBuilder();
      for (ConstraintViolation<T> contraintes : constraintViolations) {
        errors.append(
            String.format(
                "%s.%s %s\n",
                contraintes.getRootBeanClass().getSimpleName(),
                contraintes.getPropertyPath(),
                contraintes.getMessage()));
      }
      throw new PlatformServiceException(BAD_REQUEST, "Bean validation : " + errors);
    }
  }

  public static List<String> parametersToList(String collectionFormat, String[] values) {
    List<String> params = new ArrayList<>();

    if (values == null) {
      return params;
    }

    if (values.length >= 1 && collectionFormat.equals("multi")) {
      params.addAll(Arrays.asList(values));
    } else {
      collectionFormat =
          (collectionFormat == null || collectionFormat.isEmpty()
              ? "csv"
              : collectionFormat); // default: csv

      String delimiter = ",";

      switch (collectionFormat) {
        case "csv":
          {
            delimiter = ",";
            break;
          }
        case "ssv":
          {
            delimiter = " ";
            break;
          }
        case "tsv":
          {
            delimiter = "\t";
            break;
          }
        case "pipes":
          {
            delimiter = "|";
            break;
          }
      }

      params = Arrays.asList(values[0].split(delimiter));
    }

    return params;
  }

  public static String parameterToString(Object param) {
    if (param == null) {
      return "";
    } else if (param instanceof Date) {
      return formatDatetime((Date) param);
    } else if (param instanceof Collection) {
      StringBuilder b = new StringBuilder();
      for (Object o : (Collection) param) {
        if (b.length() > 0) {
          b.append(",");
        }
        b.append(String.valueOf(o));
      }

      return b.toString();
    } else {
      return String.valueOf(param);
    }
  }

  public static String formatDatetime(Date date) {
    return new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSSXXX", Locale.ROOT).format(date);
  }
}
