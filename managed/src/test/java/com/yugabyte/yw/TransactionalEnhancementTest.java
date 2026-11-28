// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw;

import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.models.Customer;
import io.ebean.annotation.Transactional;
import io.ebean.bean.EnhancedTransactional;
import java.net.URL;
import java.util.Set;
import java.util.TreeSet;
import org.junit.Test;
import org.reflections.Reflections;
import org.reflections.scanners.Scanners;
import org.reflections.util.ClasspathHelper;
import org.reflections.util.ConfigurationBuilder;

/**
 * {@code @Transactional} does nothing unless the Ebean agent enhances the class. The build runs the
 * agent only on the packages in playEbeanModels (build.sbt), and the agent enhances
 * {@code @Transactional} only in the transactional-packages of ebean.mf. It marks each class it
 * enhances with {@link EnhancedTransactional}; this test fails for any main class using the
 * annotation without the mark.
 */
public class TransactionalEnhancementTest {

  @Test
  public void testTransactionalClassesAreEnhanced() throws Exception {
    // The directory holding the compiled main classes, not the test classes or the jars.
    URL mainClasses = ClasspathHelper.forClass(Customer.class);
    Reflections reflections =
        new Reflections(
            new ConfigurationBuilder()
                .setUrls(mainClasses)
                .setScanners(Scanners.MethodsAnnotated, Scanners.TypesAnnotated));

    Set<String> classNames = new TreeSet<>();
    for (String method : reflections.get(Scanners.MethodsAnnotated.with(Transactional.class))) {
      String qualifiedMethod = method.substring(0, method.indexOf('('));
      classNames.add(qualifiedMethod.substring(0, qualifiedMethod.lastIndexOf('.')));
    }
    classNames.addAll(reflections.get(Scanners.TypesAnnotated.with(Transactional.class)));
    assertTrue(
        "Found no @Transactional classes in " + mainClasses,
        classNames.stream().anyMatch(name -> name.startsWith("com.yugabyte.yw."))
            && classNames.stream().anyMatch(name -> name.startsWith("api.v2.")));

    Set<String> notEnhanced = new TreeSet<>();
    ClassLoader loader = getClass().getClassLoader();
    for (String className : classNames) {
      Class<?> clazz = Class.forName(className, false /* initialize */, loader);
      if (!clazz.isInterface() && !EnhancedTransactional.class.isAssignableFrom(clazz)) {
        notEnhanced.add(className);
      }
    }
    assertTrue(
        "@Transactional has no effect in these classes; add their package to playEbeanModels in"
            + " managed/build.sbt and to transactional-packages in"
            + " managed/src/main/resources/ebean.mf: "
            + notEnhanced,
        notEnhanced.isEmpty());
  }
}
