/*
 * Created on Mon Mar 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { jsx } from 'slate-hyperscript';
import {
  CustomElement,
  CustomText,
  DefaultElement,
  DOMElement,
  IYBEditor,
  IYBSlatePluginReturnProps
} from '../../plugins';

export class HTMLDeSerializer {
  HTMLDocument: Document;
  editor: IYBEditor;

  constructor(editor: IYBEditor, HTMLstr: string) {
    this.HTMLDocument = new DOMParser().parseFromString(HTMLstr, 'text/html');
    this.editor = editor;
  }

  deserialize() {
    return this.deserializeElementsToComponent(this.HTMLDocument.body) ?? [];
  }

  deserializeElementsToComponent(el: DOMElement, markAttributes = {}) {
    if (el.nodeType === Node.TEXT_NODE) {
      return jsx('text', markAttributes, el.textContent);
    }

    if (el.nodeType !== Node.ELEMENT_NODE) {
      return null;
    }

    let children: any[] = Array.from(el.childNodes)
      .map((node) => this.deserializeElementsToComponent(node as DOMElement, markAttributes))
      .flat();

    if (children.length === 0) {
      children = [{ text: '', ...markAttributes }];
    }

    if (el.nodeName === document.body.nodeName) {
      return jsx('fragment', markAttributes, children);
    }

    return this.getElementFromNode(el, markAttributes, children);
  }

  getElementFromNode(
    node: DOMElement,
    markAttributes = {},
    children: Element[] | Node[]
  ): CustomElement | CustomText {
    for (const plugin of this.editor['pluginsList'] as IYBSlatePluginReturnProps[]) {
      const Element = plugin.deSerialize?.(node, markAttributes, children);
      if (Element !== undefined) {
        return Element;
      }
    }
    return DefaultElement;
  }
}
