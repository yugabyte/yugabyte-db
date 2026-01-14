// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.forms.AbstractTaskParams;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;
import java.util.function.Predicate;
import org.junit.Before;
import org.junit.Test;
import org.reflections.Reflections;
import org.reflections.util.ConfigurationBuilder;

/**
 * This test checks that no subclass hides fields from its superclass by declaring a public or
 * protected field with the same name. This does not consider getters and setters at the moment.
 */
public class DetectHiddenFieldTest {

  // Packages to scan for subclasses.
  private static final Set<String> PACKAGES_TO_SCAN = ImmutableSet.of("com.yugabyte.yw");

  // Add the super-classes to check for hidden fields in their subclasses.
  private static final Set<Class<?>> SUPPER_CLASSES_TO_CHECK =
      ImmutableSet.of(AbstractTaskParams.class);

  // Field modifier checker for hiding.
  private static final Predicate<Integer> MODIFIERS_PREDICATE =
      m -> !(Modifier.isStatic(m) || Modifier.isPrivate(m));

  // Class to its TreeNode mapping for the class hierarchy.
  private final Map<Class<?>, TreeNode> treeNodes = new HashMap<>();

  // Tree node representing a class and its subclass nodes.
  static class TreeNode {
    Class<?> clazz;
    Set<TreeNode> children = new HashSet<>();

    TreeNode(Class<?> clazz) {
      this.clazz = clazz;
    }
  }

  @Before
  public void setup() {
    treeNodes.clear();
    constructTree();
  }

  @SuppressWarnings("unchecked")
  private void constructTree() {
    ConfigurationBuilder configBuilder =
        new ConfigurationBuilder().forPackages(PACKAGES_TO_SCAN.toArray(new String[0]));
    Reflections reflections = new Reflections(configBuilder);
    for (Class<?> rootClass : SUPPER_CLASSES_TO_CHECK) {
      Set<Class<?>> subClasses = reflections.getSubTypesOf((Class<Object>) rootClass);
      for (Class<?> subClass : subClasses) {
        Class<?> superClass = subClass.getSuperclass();
        // Super class to subclasses.
        treeNodes
            .computeIfAbsent(superClass, k -> new TreeNode(k))
            .children
            .add(treeNodes.computeIfAbsent(subClass, k -> new TreeNode(k)));
      }
    }
  }

  private void dfsCheckSubclasses(TreeNode root, Set<String> fields, Set<String> errors) {
    for (TreeNode childNode : root.children) {
      Set<String> accumulatedFields = new HashSet<>(fields);
      if (childNode != null) {
        Class<?> superClass = root.clazz;
        Class<?> childClass = childNode.clazz;
        for (Field field : childClass.getDeclaredFields()) {
          if (fields.contains(field.getName())) {
            errors.add(
                String.format(
                    "Class %s hides field %s from superclass %s",
                    childClass.getName(), field.getName(), superClass.getName()));
          }
          if (MODIFIERS_PREDICATE.test(field.getModifiers())) {
            // Accumulate the fields for further subclasses.
            accumulatedFields.add(field.getName());
          }
        }
        // Pass down the accumulated fields to subclasses.
        dfsCheckSubclasses(childNode, accumulatedFields, errors);
      }
    }
  }

  private void checkAllSubclasses() {
    Set<String> errors = new LinkedHashSet<>();
    for (Class<?> rootClass : SUPPER_CLASSES_TO_CHECK) {
      TreeNode root = treeNodes.get(rootClass);
      if (root != null) {
        Set<String> fields = new HashSet<>();
        for (Field field : root.clazz.getDeclaredFields()) {
          if (MODIFIERS_PREDICATE.test(field.getModifiers())) {
            fields.add(field.getName());
          }
        }
        dfsCheckSubclasses(root, fields, errors);
      }
    }
    if (!errors.isEmpty()) {
      StringBuilder sb = new StringBuilder();
      sb.append(errors.size()).append(" hidden fields detected:\n");
      for (String error : errors) {
        sb.append(error).append("\n");
      }
      throw new AssertionError(sb.toString());
    }
  }

  @Test
  public void testForHiddenFields() {
    checkAllSubclasses();
  }
}
