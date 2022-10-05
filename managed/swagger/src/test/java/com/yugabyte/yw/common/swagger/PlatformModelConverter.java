package com.yugabyte.yw.common.swagger;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.google.common.collect.ImmutableSet;
import io.swagger.converter.ModelConverter;
import io.swagger.converter.ModelConverterContext;
import io.swagger.converter.ModelConverters;
import io.swagger.models.Model;
import io.swagger.models.properties.Property;
import java.lang.annotation.Annotation;
import java.lang.reflect.Type;
import java.util.Arrays;
import java.util.Iterator;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PlatformModelConverter implements ModelConverter {

  static final PlatformModelConverter SINGLETON = new PlatformModelConverter();

  static void register() {
    // remove if one is already registers to avoid duplicates in tests
    ModelConverters.getInstance().removeConverter(SINGLETON);
    ModelConverters.getInstance().addConverter(SINGLETON);
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
    return nextConverter.resolveProperty(type, context, annotations, chain);
  }

  private boolean canSkip(Annotation[] annotations) {
    if (annotations == null) {
      return false;
    }
    return Arrays.stream(annotations)
        .anyMatch(annotation -> annotation.annotationType().equals(JsonBackReference.class));
  }

  @Override
  public Model resolve(Type type, ModelConverterContext context, Iterator<ModelConverter> chain) {
    if (canSkip(type)) {
      return null;
    }
    return chain.next().resolve(type, context, chain);
  }

  private boolean canSkip(Type type) {
    String typeName = type.getTypeName();
    return SKIPPED_PACKAGES.stream().anyMatch(typeName::contains);
  }
}
