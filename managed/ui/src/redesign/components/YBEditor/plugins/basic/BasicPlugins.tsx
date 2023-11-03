/*
 * Created on Fri Mar 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React from 'react';

import { Element as SlateElement, Text, Node as SlateNode } from 'slate';
import { jsx } from 'slate-hyperscript';
import { CustomElement, CustomText, DOMElement, TextDecorators } from '../custom-types';
import { IYBSlatePlugin, SlateRenderElementProps, SlateRenderLeafProps } from '../IPlugin';
import { nonActivePluginReturnType, toggleBlock, toggleMark } from '../PluginUtils';

const PLUGIN_NAME = 'Basic';
/**
 * basic plugin - supports basic functionalities like bold, italics, underline etc..
 */
export const useBasicPlugins: IYBSlatePlugin = ({ enabled, editor }) => {
  const editorRef = editor;

  if (!enabled) {
    return { name: PLUGIN_NAME, ...nonActivePluginReturnType };
  }

  /**
   * custom function to add/remove marks like bold, italics etc
   */
  editor['toggleMark'] = (mark: TextDecorators) => {
    toggleMark(editorRef, mark);
  };

  /**
   * custom function to add/remove elements like code, paragraph
   */

  editor['toggleBlock'] = (block: CustomElement['type']) => {
    toggleBlock(editorRef, block);
  };

  const renderElement = ({ attributes, children, element }: SlateRenderElementProps) => {
    switch (element.type) {
      case 'paragraph':
        return (
          <p {...attributes} style={{ textAlign: element.align }}>
            {children}
          </p>
        );
      case 'heading':
        return <h1 {...attributes}>{children}</h1>;
      case 'list-item':
        return <ul {...attributes}>{children}</ul>;
      default:
        return undefined;
    }
  };

  const renderLeaf = ({ attributes, children, text }: SlateRenderLeafProps) => {
    if (!text.bold && !text.italic && !text.underline && !text.strikethrough) return undefined;

    if (text.bold) {
      children = <strong>{children}</strong>;
    }

    if (text.italic) {
      children = <em>{children}</em>;
    }

    if (text.underline) {
      children = <u>{children}</u>;
    }

    if (text.strikethrough) {
      children = <s>{children}</s>;
    }

    return <span {...attributes}>{children}</span>;
  };

  const onKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    return false;
  };

  return {
    name: PLUGIN_NAME,
    renderElement,
    onKeyDown,
    isEnabled: () => enabled,
    renderLeaf,
    defaultComponents: [],
    serialize,
    deSerialize
  };
};

const serialize = (node: CustomElement | CustomText, children: string) => {
  if (Text.isText(node)) {
    if (node.bold || node.italic || node.strikethrough || node.underline) {
      let string = node.text;
      if (node.bold) {
        string = `<strong>${string}</strong>`;
      }
      if (node.italic) {
        string = `<em>${string}</em>`;
      }
      if (node.underline) {
        string = `<u>${string}</u>`;
      }
      if (node.strikethrough) {
        string = `<s>${string}</s>`;
      }
      return string;
    }

    return node.text;
  }

  switch (node.type) {
    case 'paragraph':
      if (node.align) {
        return `<p align="${node.align}">${children}</p>`;
      }
      return `<p>${children}</p>`;
    case 'heading':
      return `<h1>${children}</h1>`;
    default:
      return undefined;
  }
};

const deSerialize = (
  el: DOMElement,
  markAttributes = {},
  children: SlateElement[] | Node[]
): CustomElement | CustomText | undefined => {
  switch (el.nodeName) {
    case 'P':
      return jsx('element', { type: 'paragraph', align: el.getAttribute('align') }, children);
    case 'H1':
      return jsx('element', { type: 'heading' }, children);
    case 'STRONG':
      return jsx('text', { ...markAttributes, bold: true, text: el.textContent }, children);
    case 'EM':
      return jsx('text', { ...markAttributes, italic: true, text: el.textContent }, children);
    case 'U':
      return jsx('text', { ...markAttributes, underline: true, text: el.textContent }, children);
    case 'S':
      return jsx(
        'text',
        { ...markAttributes, strikethrough: true, text: el.textContent },
        children
      );
    default:
      return undefined;
  }
};
