/*
 * Created on Mon Apr 10 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { IYBSlatePlugin, SlateRenderElementProps } from '../IPlugin';
import { nonActivePluginReturnType, serializeToText } from '../PluginUtils';

const PLUGIN_NAME = 'default plugin';

export const useDefaultPlugin: IYBSlatePlugin = ({ enabled, editor }) => {
  if (!enabled) {
    return { name: PLUGIN_NAME, ...nonActivePluginReturnType };
  }

  return {
    name: PLUGIN_NAME,
    isEnabled: () => enabled,
    onKeyDown: () => false,
    renderElement: ({ attributes, children, element }: SlateRenderElementProps) => (
      <p {...attributes}>{children}</p>
    ),
    renderLeaf: () => undefined,
    serialize: serializeToText
  };
};
