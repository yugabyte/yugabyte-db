/*
 * Created on Fri Mar 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React from 'react';

import { IYBSlatePlugin, SlateRenderElementProps, SlateRenderLeafProps } from './IPlugin';
import { CustomElement, TextDecorators } from './custom-types';
import { toggleBlock, toggleMark } from './PluginUtils';

/**
 * basic plugin - supports basic functionalities like bold, italics, underline etc..
 */
export const useBasicPlugins: IYBSlatePlugin = ({ editor }) => {
  const editorRef = editor;

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
          <p {...attributes} style={{ textAlign: element.align as any }}>
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
    name: 'Basic Plugin',
    renderElement,
    onKeyDown,
    renderLeaf
  };
};
