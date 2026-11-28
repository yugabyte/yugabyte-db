package com.yugabyte.yw.common.swagger;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.base.Strings;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.models.common.YBADeprecated;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.converter.ModelConverter;
import io.swagger.converter.ModelConverterContext;
import io.swagger.converter.ModelConverters;
import io.swagger.models.Model;
import io.swagger.models.properties.DateTimeProperty;
import io.swagger.models.properties.Property;
import io.swagger.util.Json;
import java.lang.annotation.Annotation;
import java.lang.reflect.Field;
import java.lang.reflect.Type;
import java.util.Arrays;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ConcurrentSkipListSet;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PlatformModelConverter implements ModelConverter {

  static final PlatformModelConverter SINGLETON = new PlatformModelConverter();

  // option can look like one of - "all", "24m", "2.16.1.0"
  // should be set before calling #register() method, otherwise this option is ignored
  public static String excludeYbaDeprecatedOption;
  private static YBADeprecationProcessor deprecationProcessor;

  public static String excludeYbaInternalOption;
  private static YBAInternalProcessor internalProcessor;

  static void register() {
    // remove if one is already registers to avoid duplicates in tests
    ModelConverters.getInstance().removeConverter(SINGLETON);
    ModelConverters.getInstance().addConverter(SINGLETON);
    deprecationProcessor = new YBADeprecationProcessor(excludeYbaDeprecatedOption);
    internalProcessor = new YBAInternalProcessor(excludeYbaInternalOption);
  }

  private static final ImmutableSet<String> SKIPPED_PACKAGES =
      ImmutableSet.of("play.mvc", "io.ebean", "com.fasterxml.jackson.databind");
  public static final Logger LOG = LoggerFactory.getLogger(PlatformModelConverter.class);

  @Override
  public Property resolveProperty(
      Type type,
      ModelConverterContext context,
      Annotation[] annotations,
      Iterator<ModelConverter> chain) {
    if (canSkip(type) || canSkip(annotations) || !chain.hasNext()) {
      LOG.debug("skipped {}", type.getTypeName());
      return null;
    }

    ModelConverter nextConverter = chain.next();
    if (nextConverter == this) {
      LOG.warn("{}{}", type.getTypeName(), annotations);
      throw new RuntimeException("Duplicate YWModelConverter added");
    }
    Property property = nextConverter.resolveProperty(type, context, annotations, chain);
    checkDateTime(property, annotations);
    return property;
  }

  private boolean canSkip(Annotation[] annotations) {
    if (annotations == null) {
      return false;
    }
    if (canSkipDeprecated(annotations)) {
      LOG.info("Skipping due to annotation");
      return true;
    }
    return Arrays.stream(annotations)
        .anyMatch(annotation -> annotation.annotationType().equals(JsonBackReference.class));
  }

  private boolean hasYBADeprecated(Annotation[] annotations) {
    if (annotations == null) {
      return false;
    }
    return Arrays.stream(annotations)
        .anyMatch(
            a ->
                a.annotationType().equals(YBADeprecated.class)
                    || (a.annotationType().equals(YbaApi.class)
                        && ((YbaApi) a).visibility() == YbaApiVisibility.DEPRECATED));
  }

  private void checkDateTime(Property property, Annotation[] annotations) {
    if (hasYBADeprecated(annotations)) {
      // Skip date-time check if the property is already YBADeprecated
      return;
    }
    if (property instanceof DateTimeProperty) {
      DateTimeProperty dtp = (DateTimeProperty) property;
      Optional<Annotation> jsonFormatAnnotationOpt =
          Arrays.stream(annotations)
              .filter(a -> a.annotationType().equals(JsonFormat.class))
              .findFirst();
      String errMsg =
          String.format(
              "A DateTime property should have annotation @JsonFormat(shape ="
                  + " JsonFormat.Shape.STRING, pattern = \"yyyy-MM-dd'T'HH:mm:ss'Z'\")");
      try {
        JsonFormat j = (JsonFormat) jsonFormatAnnotationOpt.get();
        if (Strings.isNullOrEmpty(j.pattern()) || !j.pattern().equals("yyyy-MM-dd'T'HH:mm:ss'Z'")) {
          throw new RuntimeException(errMsg);
        }
      } catch (NoSuchElementException e) {
        throw new RuntimeException(errMsg);
      }
    }
  }

  @Override
  public Model resolve(Type type, ModelConverterContext context, Iterator<ModelConverter> chain) {
    if (canSkip(type)) {
      return null;
    }
    Model model = chain.next().resolve(type, context, chain);
    findNullableRequired(type, model);
    return model;
  }

  // Swagger ignores @Nullable: a field without @ApiModelProperty is required, so the generated
  // clients make it a required constructor argument.
  private static final Set<String> NULLABLE_REQUIRED = new ConcurrentSkipListSet<>();

  /** Returns "Class.field" for each @Nullable field that the spec lists as required. */
  public static Set<String> nullableRequiredFields() {
    return NULLABLE_REQUIRED;
  }

  private void findNullableRequired(Type type, Model model) {
    if (model == null || model.getProperties() == null) {
      return;
    }
    Class<?> cls = Json.mapper().constructType(type).getRawClass();
    for (Class<?> c = cls; c != null && c != Object.class; c = c.getSuperclass()) {
      for (Field field : c.getDeclaredFields()) {
        boolean nullable =
            Arrays.stream(field.getAnnotations())
                .anyMatch(a -> a.annotationType().getSimpleName().equals("Nullable"));
        if (!nullable) {
          continue;
        }
        JsonProperty jp = field.getAnnotation(JsonProperty.class);
        String name = jp != null && !jp.value().isEmpty() ? jp.value() : field.getName();
        Property property = model.getProperties().get(name);
        if (property != null && property.getRequired()) {
          // The declaring class, so an inherited field is reported once.
          NULLABLE_REQUIRED.add(c.getName() + "." + field.getName());
        }
      }
    }
  }

  private boolean canSkip(Type type) {
    String typeName = type.getTypeName();
    return SKIPPED_PACKAGES.stream().anyMatch(typeName::contains);
  }

  // Returns true when the given list contains at least one YBADeprecated annotation that has its
  // sinceDate falling after the condition set in excludeYbaDeprecatedOption, or its sinceVersion
  // greater than the version set in excludeYbaDeprecatedOption.
  private boolean canSkipDeprecated(Annotation[] annotations) {
    Optional<YBADeprecated> ybaDeprecatedAnn =
        Arrays.stream(annotations)
            .filter(annotation -> annotation.annotationType().equals(YBADeprecated.class))
            .map(annotation -> (YBADeprecated) annotation)
            .findFirst();
    if (ybaDeprecatedAnn.isPresent()) {
      YBADeprecated ybaDeprecatedAnnotation = ybaDeprecatedAnn.get();
      return deprecationProcessor.shouldExcludeDeprecated(ybaDeprecatedAnnotation);
    }
    Optional<YbaApi> ybaApiAnn =
        Arrays.stream(annotations)
            .filter(annotation -> annotation.annotationType().equals(YbaApi.class))
            .map(annotation -> (YbaApi) annotation)
            .findFirst();
    if (ybaApiAnn.isPresent() && ybaApiAnn.get().visibility() == YbaApiVisibility.DEPRECATED) {
      // TODO: Update deprecationProcessor to handle sinceDate from YbaApi
      // Until then consider as "deprecate all"
      return deprecationProcessor.shouldExcludeDeprecated(null);
    }
    return false;
  }
}
