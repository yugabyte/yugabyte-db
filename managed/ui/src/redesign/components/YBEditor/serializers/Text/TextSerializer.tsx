/*
 * Created on Mon Apr 10 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Descendant } from "slate";
import { IYBEditor, serializeToText } from "../../plugins";

/**
 * convert slatejs nodes to text
 */
export class TextSerializer {

    editor: IYBEditor;

    constructor(editor: IYBEditor) {
        this.editor = editor;
    }

    serialize = () => {
        return this.editor.children.map((child) => serializeToText(child)).join('\n');
    }

    serlializeElements = (nodes: Descendant[]) => {
        return nodes.map((child) => serializeToText(child)).join('\n');
    }

}
