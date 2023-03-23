/*
 * Created on Fri Mar 10 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Editor, Transforms, Element as SlateElement } from 'slate';
import { CustomElement, IYBEditor, TextDecorators } from './custom-types';
import {
  IYBSlatePluginReturnProps,
  SlateRenderElementProps,
  SlateRenderLeafProps
} from './IPlugin';

/**
 * common function which can be used to return for non enabled plugins.
 */
export const nonActivePluginReturnType: Omit<IYBSlatePluginReturnProps, 'name'> = {
  renderElement: (props: SlateRenderElementProps) => undefined,

  onKeyDown: (_e: React.KeyboardEvent<HTMLDivElement>) => false,

  renderLeaf: (_props: SlateRenderLeafProps) => undefined
};

const LIST_TYPES = ['numbered-list', 'bulleted-list'];
const TEXT_ALIGN_TYPES = ['left', 'center', 'right', 'justify'];

/**
 * check if the current block is active
 */
export const isBlockActive = (editor: IYBEditor, format: string, blockType = 'type') => {
  const { selection } = editor;
  if (!selection) return false;

  const [match] = Array.from(
    Editor.nodes(editor, {
      at: Editor.unhangRange(editor, selection),
      match: (n) => !Editor.isEditor(n) && SlateElement.isElement(n) && n[blockType] === format
    })
  );

  return !!match;
};

export const isMarkActive = (editor: IYBEditor, mark: CustomElement['type']) => {
  const marks = Editor.marks(editor);
  return marks ? marks[mark] === true : false;
};

export const toggleBlock = (editor: IYBEditor, block: string) => {
  const isActive = isBlockActive(
    editor,
    block,
    TEXT_ALIGN_TYPES.includes(block) ? 'align' : 'type'
  );

  const isList = LIST_TYPES.includes(block);

  Transforms.unwrapNodes(editor, {
    match: (n) =>
      !Editor.isEditor(n) &&
      SlateElement.isElement(n) &&
      LIST_TYPES.includes(n.type) &&
      !TEXT_ALIGN_TYPES.includes(block),
    split: true
  });
  let newProperties: Partial<SlateElement>;
  if (TEXT_ALIGN_TYPES.includes(block)) {
    newProperties = {
      align: isActive ? undefined : block
    };
  } else {
    newProperties = {
      type: isActive ? 'paragraph' : isList ? 'list-item' : block
    };
  }
  Transforms.setNodes<SlateElement>(editor, newProperties);

  if (!isActive && isList) {
    const b = { type: block, children: [] };
    Transforms.wrapNodes(editor, b);
  }
};

export const toggleMark = (editor: IYBEditor, mark: TextDecorators) => {
  const isActive = isMarkActive(editor, mark);

  if (isActive) {
    Editor.removeMark(editor, mark);
  } else {
    Editor.addMark(editor, mark, true);
  }
};
