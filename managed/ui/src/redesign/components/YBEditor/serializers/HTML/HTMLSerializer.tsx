/*
 * Created on Mon Mar 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Descendant, Text } from 'slate';
import { CustomText, CustomElement, IYBSlatePluginReturnProps, IYBEditor } from '../../plugins';

/**
 * Serializes editor contents to HTML
 */
export class HTMLSerializer {
  editor: IYBEditor;
  constructor(editor: IYBEditor) {
    this.editor = editor;
  }

  serialize() {
    return this.editor.children.map((child) => this.serializeElementToHTML(child)).join('');
  }

  serializeElement(nodes: Descendant[]) {
    return nodes.map((child) => this.serializeElementToHTML(child)).join('');
  }

  serializeElementToHTML(node: CustomText | CustomElement): string {
    if (Text.isText(node)) {
      let leafHtml = this.getParsedHTMLFromPlugins(node, '');
      return leafHtml ?? '';
    }

    const children = node.children.map((n) => this.serializeElementToHTML(n)).join('') ?? [];

    return this.getParsedHTMLFromPlugins(node, children) ?? '';
  }

  /**
   * loop through all plugins and get the parsed html
   */
  getParsedHTMLFromPlugins(node: CustomText | CustomElement, children: string): string {
    for (const plugin of this.editor['pluginsList'] as IYBSlatePluginReturnProps[]) {
      const serializedHTML = plugin.serialize?.(node, children);
      if (serializedHTML !== undefined) {
        return serializedHTML;
      }
    }
    // if no plugin can handle the node, just return and empty string(i.e) skip the node
    return '';
  }
}
