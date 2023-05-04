/*
 * Created on Tue May 02 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Text } from 'slate';
import { Descendant } from 'slate';
import { ALERT_VARIABLE_END_TAG, ALERT_VARIABLE_START_TAG } from '../../plugins';

export const convertNodesToText = (nodes: Descendant[]): string => {
  return nodes
    .map((node) => {
      if (Text.isText(node)) {
        return node.text;
      }
      if (node.type === 'alertVariable') {
        return `${ALERT_VARIABLE_START_TAG}${node.variableName}${ALERT_VARIABLE_END_TAG}`;
      }
      const text = convertNodesToText(node.children);
      return text;
    })
    .join('');
};
