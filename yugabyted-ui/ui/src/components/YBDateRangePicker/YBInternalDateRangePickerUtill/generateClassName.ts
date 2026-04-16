import type { Rule, StyleSheet } from 'jss';

let ruleCounter = 0;

// Adds a prefix to all generated class names, to avoid conflict with other Material UI instances.
const prefix = 'materialui-daterange-picker';

export const generateClassName = (rule: Rule, sheet?: StyleSheet<string>): string => {
  ruleCounter += 1;

  if (sheet?.options.meta) {
    return `${prefix}-${sheet.options.meta}-${rule.key}-${ruleCounter}`;
  }

  return `${prefix}-${rule.key}-${ruleCounter}`;
};
