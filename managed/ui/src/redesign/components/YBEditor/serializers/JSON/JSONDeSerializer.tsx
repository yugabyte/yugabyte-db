/*
 * Created on Thu Apr 06 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { jsx } from "slate-hyperscript";
import { IYBEditor } from "../../plugins";
import { JSON_BLOCK_TYPE } from "../../plugins/json/JSONPlugin";

export class JSONDeserializer {

    editor: IYBEditor;
    JSONStr: string;

    constructor(editor: IYBEditor, JSONStr: string) {
        this.editor = editor;
        this.JSONStr = JSONStr;
    }

    deserialize() {
        return this.JSONStr.split('\n').map((text) => {
            return jsx(
                'element',
                {
                    type: JSON_BLOCK_TYPE,
                },
                [{text}]
            );
        });
    }

}
