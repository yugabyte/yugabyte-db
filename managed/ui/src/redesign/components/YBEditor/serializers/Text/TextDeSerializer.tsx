/*
 * Created on Mon Apr 10 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { jsx } from "slate-hyperscript";
import { IYBEditor } from "../../plugins";

/**
 * convert text to Slate js elements
 */

export class TextDeserializer {

    editor: IYBEditor;
    str: string;

    constructor(editor: IYBEditor, str: string) {
        this.editor = editor;
        this.str = str;
    }

    deserialize() {
        return this.str.split('\n').map((text) => {
            return jsx(
                'element',
                {
                    type: 'paragraph',
                },
                [{text}]
            );
        });
    }

}
