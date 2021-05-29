package com.yugabyte.yw.common.swagger;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Singleton;
import io.swagger.converter.ModelConverter;
import io.swagger.converter.ModelConverterContext;
import io.swagger.converter.ModelConverters;
import io.swagger.models.Model;
import io.swagger.models.properties.Property;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.lang.annotation.Annotation;
import java.lang.reflect.Type;
import java.util.Iterator;

@Singleton
public class YWModelConverter implements ModelConverter {

  static final YWModelConverter SINGLETON = new YWModelConverter();

  static void register() {
    // remove if one is already registers to avoid duplicates in tests
    ModelConverters.getInstance().removeConverter(SINGLETON);
    ModelConverters.getInstance().addConverter(SINGLETON);
  }

  private static final ImmutableSet<String> SKIPPED_PACKAGES =
      ImmutableSet.of("play.mvc", "io.ebean");
  public static final Logger LOG = LoggerFactory.getLogger(YWModelConverter.class);
  private static final double maxLevel = 300;

  @Override
  public Property resolveProperty(
      Type type,
      ModelConverterContext context,
      Annotation[] annotations,
      Iterator<ModelConverter> chain) {
    if (tooDeep()) {
      LOG.warn("{}{}", type.getTypeName(), annotations);
      throw new RuntimeException("Too Deep");
    }
    if (canSkip(type) || !chain.hasNext()) {
      LOG.debug("skipped {}", type.getTypeName());
      return null;
    }
    return chain.next().resolveProperty(type, context, annotations, chain);
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

  private static boolean tooDeep() {
    try {
      throw new IllegalStateException("Too deep, emerging");
    } catch (IllegalStateException e) {
      if (e.getStackTrace().length > maxLevel + 1) {
        return true;
      }
    }
    return false;
  }
}
