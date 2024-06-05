/*
 * Created on Tue Mar 14 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React from 'react';
import { Transforms } from 'slate';
import { IYBSlatePlugin, SlateRenderElementProps, SlateRenderLeafProps } from './IPlugin';
import { IYBEditor } from './custom-types';
import { nonActivePluginReturnType } from './PluginUtils';

const PLUGIN_NAME = 'Single Line Plugin';

/**
 *
 * Plugin which restricts the editor to single line
 */

export const useSingleLinePlugin: IYBSlatePlugin = ({ editor, enabled }) => {
  if (!enabled) {
    return { name: PLUGIN_NAME, ...nonActivePluginReturnType };
  }

  const init = () => {
    const { normalizeNode } = editor;

    editor.normalizeNode = ([node, path]) => {
      if (path.length === 0) {
        if (editor.children.length > 1) {
          Transforms.mergeNodes(editor as IYBEditor);
        }
      }

      return normalizeNode([node, path]);
    };
  };

  const renderElement = (_props: SlateRenderElementProps) => undefined;

  const onKeyDown = (_e: React.KeyboardEvent<HTMLDivElement>) => false;

  const renderLeaf = (_props: SlateRenderLeafProps) => undefined;

  return {
    name: PLUGIN_NAME,
    init,
    renderElement,
    onKeyDown,
    renderLeaf,
    isEnabled: () => enabled
  };
};
