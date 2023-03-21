/*
 * Created on Fri Mar 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React from 'react';
import { useQuery } from 'react-query';
import { IYBSlatePlugin, SlateRenderElementProps, SlateRenderLeafProps } from './IPlugin';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  fetchAlertTemplateVariables
} from '../../../features/alerts/TemplateComposer/CustomVariablesAPI';
import { nonActivePluginReturnType } from './PluginUtils';

const PLUGIN_NAME = 'Custom Alert Variables Plugin';

export const useAlertVariablesPlugin: IYBSlatePlugin = ({ editor: _editor, enabled }) => {
  useQuery(ALERT_TEMPLATES_QUERY_KEY.fetchAlertTemplateVariables, fetchAlertTemplateVariables, {
    enabled
  });

  if (!enabled) {
    return { name: PLUGIN_NAME, ...nonActivePluginReturnType };
  }

  const renderElement = ({ attributes, children, element }: SlateRenderElementProps) => {
    switch (element.type) {
      case 'paragraph':
        return <p {...attributes}>{children}</p>;
      case 'heading':
        return <h1 {...attributes}>{children}</h1>;
      case 'code':
        return <pre {...attributes}>{children}</pre>;
      default:
        return undefined;
    }
  };
  const onKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    return false;
  };

  const renderLeaf = (_props: SlateRenderLeafProps) => {
    return undefined;
  };
  return {
    name: PLUGIN_NAME,
    renderElement,
    onKeyDown,
    renderLeaf
  };
};
