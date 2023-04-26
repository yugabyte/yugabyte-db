/*
 * Created on Tue Mar 07 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import { useMount } from 'react-use';
import { NodeEntry } from 'slate';

import { useBasicPlugins } from './basic/BasicPlugins';
import { CustomText, IYBEditor } from './custom-types';
import { useAlertVariablesPlugin } from './alert/AlertVariablesPlugin';
import {
  IYBSlatePluginReturnProps,
  SlateRenderElementProps,
  SlateRenderLeafProps
} from './IPlugin';
import { useSingleLinePlugin } from './SingleLinePlugin';
import { useJSONPlugin } from './json/JSONPlugin';
import { useDefaultPlugin } from './default/DefaultPlugin';

export type LoadPlugins = {
  basic?: boolean;
  alertVariablesPlugin?: boolean;
  singleLine?: boolean;
  jsonPlugin?: boolean;
  defaultPlugin?: boolean;
};

/**
 * Plugin Manager is responsible for loading the plugins to the slate editor
 */

export function useEditorPlugin(editor: IYBEditor, loadPlugins: LoadPlugins) {
  /**
   * Initialise the plugins
   */
  let pluginsList: IYBSlatePluginReturnProps[] = [
    useBasicPlugins({ editor, enabled: loadPlugins.basic }),
    useAlertVariablesPlugin({ editor, enabled: loadPlugins.alertVariablesPlugin }),
    useSingleLinePlugin({ editor, enabled: loadPlugins.singleLine }),
    useJSONPlugin({ editor, enabled: loadPlugins.jsonPlugin }),
    useDefaultPlugin({ editor, enabled: loadPlugins.defaultPlugin ?? true })
  ];

  useMount(() => {
    pluginsList = pluginsList.filter((p) => p.isEnabled());

    pluginsList.forEach((p) => p?.init?.(editor));
    editor['pluginsList'] = pluginsList; //store the list of loaded plugin's reference
  });

  /**
   * Loops through all the plugins and find the suitable plugin to render the element
   */
  function renderElement(props: SlateRenderElementProps): JSX.Element {
    for (const plugin of pluginsList) {
      const component = plugin.renderElement(props);
      if (component) return component;
    }
    return <span {...props.attributes}>{props.children}</span>;
  }

  /**
   * Loops through all the plugins and find the suitable plugin to render the leafs
   */

  function renderLeaf(props: SlateRenderLeafProps): JSX.Element {
    for (const plugin of pluginsList) {
      const component = plugin.renderLeaf(props);
      if (component) return component;
    }
    return <span {...props.attributes}>{props.children}</span>;
  }

  /**
   * Loops through all the plugins and find the suitable plugin which handles the keystroke
   */

  function onKeyDown(e: React.KeyboardEvent<HTMLDivElement>) {
    return pluginsList.some((p) => p.onKeyDown(e));
  }

  function getDefaultComponents() {
    return pluginsList.map((p) => p.defaultComponents);
  }

  function getDecorators(node: NodeEntry<CustomText>) {
    return pluginsList
      .map((p) => p.decorator?.(node))
      .flat()
      .filter(Boolean);
  }

  return { renderElement, onKeyDown, renderLeaf, getDefaultComponents, getDecorators };
}
