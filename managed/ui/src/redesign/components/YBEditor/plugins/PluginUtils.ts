/*
 * Created on Fri Mar 10 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import ReactDOM from 'react-dom';
import { head } from 'lodash';
import { Editor, Transforms, Element as SlateElement, Range, Element, Node } from 'slate';
import { ReactEditor } from 'slate-react';
import {
  CustomElement,
  CustomText,
  IYBEditor,
  JSONCodeBlock,
  Paragraph,
  TextDecorators
} from './custom-types';
import {
  IYBSlatePluginReturnProps,
  SlateRenderElementProps,
  SlateRenderLeafProps
} from './IPlugin';

export const DefaultElement: Paragraph = {
  type: 'paragraph',
  align: 'left',
  children: [{ text: '' }]
};

export const DefaultJSONElement: JSONCodeBlock = {
  type: 'jsonCode',
  children: [{ text: '' }]
};

export const ALERT_VARIABLE_REGEX = /{{\s*\w+\s*}}/g;
export const MATCH_ALL_BETWEEN_BRACKET_REGEX = /{{(.*?)}}/g;

/**
 * common function which can be used to return for non enabled plugins.
 */
export const nonActivePluginReturnType: Omit<IYBSlatePluginReturnProps, 'name'> = {
  isEnabled: () => false,

  renderElement: (_p: SlateRenderElementProps) => undefined,

  onKeyDown: (_e: React.KeyboardEvent<HTMLDivElement>) => false,

  renderLeaf: (_props: SlateRenderLeafProps) => undefined,

  defaultComponents: []
};

const TEXT_ALIGN_TYPES = ['left', 'center', 'right', 'justify'];

/**
 * check if the current block is active
 */
export const isBlockActive = (editor: IYBEditor | null, format: string, blockType = 'type') => {
  if (!editor) return false;
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

export const isMarkActive = (editor: IYBEditor | null, mark: TextDecorators) => {
  if (!editor) return false;
  const marks = Editor.marks(editor);
  return marks ? marks[mark] === true : false;
};

export const toggleBlock = (editor: IYBEditor, block: string) => {
  const isActive = isBlockActive(
    editor,
    block,
    TEXT_ALIGN_TYPES.includes(block) ? 'align' : 'type'
  );

  Transforms.unwrapNodes(editor, {
    match: (n) =>
      !Editor.isEditor(n) && SlateElement.isElement(n) && !TEXT_ALIGN_TYPES.includes(block),
    split: true
  });
  let newProperties: Partial<SlateElement>;
  if (TEXT_ALIGN_TYPES.includes(block)) {
    newProperties = {
      align: isActive ? undefined : block
    };
  } else {
    newProperties = {
      align: block
    };
  }
  Transforms.setNodes<SlateElement>(editor, newProperties);
};

export const toggleMark = (editor: IYBEditor, mark: TextDecorators) => {
  const isActive = isMarkActive(editor, mark);

  if (isActive) {
    Editor.removeMark(editor, mark);
  } else {
    Editor.addMark(editor, mark, true);
  }
};

export const Portal = ({ children }: { children: React.ReactChild | null }) => {
  return typeof document === 'object' ? ReactDOM.createPortal(children, document.body) : null;
};

export const getBeforeNChars = (editor: IYBEditor, numOfChars: number) => {
  const { selection } = editor;
  if (selection && Range.isCollapsed(selection)) {
    const { anchor } = selection;
    const block = Editor.above(editor, {
      match: (n) => SlateElement.isElement(n) && Editor.isBlock(editor, n)
    });

    const path = block ? block[1] : [];
    const start = Editor.start(editor, path);
    const range = { anchor, focus: start };
    const beforeText = Editor.string(editor, range);

    return beforeText.slice(-1 * numOfChars);
  }
  return undefined;
};

export const deleteNChars = (editor: IYBEditor, numOfChars: number, reverse = false) => {
  Transforms.delete(editor, { reverse, distance: numOfChars, unit: 'character' });
};

export const deleteElement = (editor: IYBEditor, element: CustomElement) => {
  const path = ReactEditor.findPath(editor, element);
  Transforms.removeNodes(editor, { at: path });
};

export const clearEditor = (editor: IYBEditor) => {
  Transforms.delete(editor, {
    at: {
      anchor: Editor.start(editor, []),
      focus: Editor.end(editor, [])
    }
  });
  Transforms.removeNodes(editor, {
    at: [0]
  });
};

/**
 * returns if any edit operations is made on the editor
 */
export const isEditorDirty = (editor: IYBEditor | null): boolean => {
  if (!editor) return false;
  return editor.history.undos.length !== 0;
};

/**
 * check if the given element is empty
 */
export const isEmptyElement = (element: Element): boolean => {
  if ('text' in element) return element.text === '';
  if (element.children.length > 1) return false;

  return isEmptyElement(head(element.children) as any);
};

/**
 * returns if editor is empty. (i.e) even if it has elements, it should be empty
 */
export const isEditorEmpty = (editor: IYBEditor | null): boolean => {
  if (!editor) return false;
  return editor.children.every((child) => isEmptyElement(child as CustomElement));
};

/**
 * extract text from the element
 */
export const serializeToText = (node: CustomText | CustomElement) => {
  return Node.string(node);
};

/**
 * reset the editor history
 */
export const resetEditorHistory = (editor: IYBEditor) => {
  editor.history = {
    redos: [],
    undos: []
  };
};
