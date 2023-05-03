/*
 * Created on Fri Mar 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { BaseRange, NodeEntry } from 'slate';
import { RenderElementProps, RenderLeafProps } from 'slate-react';
import { Token } from 'prismjs';
import { CustomElement, CustomText, DOMElement, IYBEditor } from './custom-types';

export interface SlateRenderElementProps extends RenderElementProps {
  element: CustomElement;
}
export interface SlateRenderLeafProps extends RenderLeafProps {
  text: CustomText;
  leaf: CustomText;
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

  /**
   * defaults components like popup, menu to be rendered in the editor
   */

  /**
   * component to be attached default to the dom. like popup, menu
   */
  defaultComponents?: { (): JSX.Element }[];

  /**
   * serializes slatejs elements to HTML
   * @param node slatejs element
   * @param children generated html string from the slatejs element for the children
   * @returns HTML string
   */
  serialize?: (node: CustomText | CustomElement, children: string) => string | undefined;

  /**
   * deSerialize html element to slatejs Element
   * @param el Dom Element
   * @param markAttributes text attributes like bold, italics
   * @param children child elements
   * @returns slatejs element or undefined
   */
  deSerialize?: (
    el: DOMElement,
    markAttributes: Omit<CustomText, 'text'>,
    children: Element[] | Node[]
  ) => CustomElement | CustomText | undefined;

  /**
   * returns whether the plugin is enabled
   * @returns true if the plugin is enabled
   */
  isEnabled: () => boolean;

  decorator?: (node: NodeEntry<CustomText>) => Partial<BaseRange>[];
};

export type IYBSlatePlugin = (props: IYBSlatePluginInputProps) => IYBSlatePluginReturnProps;
