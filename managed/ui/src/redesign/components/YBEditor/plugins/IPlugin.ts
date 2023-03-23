/*
 * Created on Fri Mar 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { RenderElementProps, RenderLeafProps } from 'slate-react';
import { CustomElement, CustomText, IYBEditor } from './custom-types';

export interface SlateRenderElementProps extends RenderElementProps {
  element: CustomElement;
}
export interface SlateRenderLeafProps extends RenderLeafProps {
  text: CustomText;
}

export type IYBSlatePluginInputProps = {
  editor: IYBEditor;
  enabled?: boolean;
};
export type IYBSlatePluginReturnProps = {
  /**
   * name of the plugin
   */
  name: string;
  /**
   * function used to assign custom functions to the editor.(Optional) custom functions can be called across all plugins
   */
  init?: (editor: IYBEditor) => void;
  /**
   * function to render custom elements like code, paragraph , custom variables etc.
   */
  renderElement: (props: SlateRenderElementProps) => JSX.Element | undefined;
  /**
   * function to render custom leaf elements bold, underline, italics etc.
   */
  renderLeaf: (props: SlateRenderLeafProps) => JSX.Element | undefined;

  /**
   * for every key press this function is called. If the plugin wants to handle such keystrokes,
   * it should return true such that we don't loop through the entire plugins. useful to implementing shortcut keys
   */

  onKeyDown: (e: React.KeyboardEvent<HTMLDivElement>) => boolean;
};

export type IYBSlatePlugin = (props: IYBSlatePluginInputProps) => IYBSlatePluginReturnProps;
