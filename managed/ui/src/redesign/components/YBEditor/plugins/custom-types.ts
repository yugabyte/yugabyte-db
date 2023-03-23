import { BaseEditor } from 'slate';
import { BaseRange, Descendant } from 'slate';
import { ReactEditor } from 'slate-react';
import { HistoryEditor } from 'slate-history';

export type CustomText = {
  bold?: boolean;
  italic?: boolean;
  underline?: boolean;
  strikethrough?: boolean;
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
  type: string;
  children: Descendant[];
};

export type IYBEditor = BaseEditor & ReactEditor & HistoryEditor;

export type CustomElement = HeadingElement | Paragraph | ListItemElement;

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
