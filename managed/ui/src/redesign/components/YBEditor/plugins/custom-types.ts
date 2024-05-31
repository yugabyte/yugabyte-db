import { BaseEditor } from 'slate';
import { BaseRange, Descendant } from 'slate';
import { ReactEditor } from 'slate-react';
import { HistoryEditor } from 'slate-history';

export type CustomText = {
  bold?: boolean;
  italic?: boolean;
  underline?: boolean;
  strikethrough?: boolean;
  decoration?: {
    [key: string]: {
      type: string;
    };
  };
  text: string;
};

export type TextDecorators = Exclude<keyof CustomText, 'text'>;

export type EmptyText = {
  text: string;
};

export type Paragraph = {
  type: 'paragraph';
  align?: 'right' | 'left' | 'center' | 'justify';
  children: Descendant[];
};

export type HeadingElement = {
  type: 'heading';
  level: number;
  children: Descendant[];
};
export type ListItemElement = {
  align?: string;
  type: 'list-item';
  children: Descendant[];
};

export type AlertVariableElement = {
  type: 'alertVariable';
  variableType: 'Custom' | 'System';
  view: 'EDIT' | 'PREVIEW' | 'NO_VALUE';
  variableName: string;
  variableValue: string;
  children: CustomText[];
};

export type JSONCodeBlock = {
  type: 'jsonCode';
  children: Descendant[];
};

export type IYBEditor = BaseEditor & ReactEditor & HistoryEditor;

export type CustomElement =
  | HeadingElement
  | Paragraph
  | AlertVariableElement
  | ListItemElement
  | JSONCodeBlock;

export type DOMElement = Element;
declare module 'slate' {
  interface CustomTypes {
    Editor: IYBEditor;
    Element: CustomElement;
    Text: CustomText | EmptyText;
    Range: BaseRange & {
      placeholder?: string;
    };
  }
}
