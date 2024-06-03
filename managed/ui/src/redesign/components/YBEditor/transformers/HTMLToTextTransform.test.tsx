/*
 * Created on Mon May 08 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { convertHTMLToText } from "./HTMLToTextTransform"

describe('HTML to text transforms test', () => {
    it('convert paragraph to simple text' ,() => {
        let text = convertHTMLToText('<p>ybeditor test</p>')
        expect(text).toEqual('ybeditor test')
    })
    it('should retain the markup tags', () => {
        let text = convertHTMLToText('<p><strong>ybeditor</strong> test</p>');
        expect(text).toEqual('<strong>ybeditor</strong> test');

        text = convertHTMLToText('<p><strong>yb<em>edit</em>or</strong> test</p>');
        expect(text).toEqual('<strong>yb<em>edit</em>or</strong> test')
    })

    it('should retain paragraph tags if align is present', ()=>{
        let text = convertHTMLToText('<p align="right"><strong>ybeditor</strong> test</p>');
        expect(text).toEqual('<p align="right"><strong>ybeditor</strong> test</p>');
    })

    it('should return variable name', ()=>{
        let text = convertHTMLToText('<span type="alertVariables">{{yb_variable}}</span>');
        expect(text).toEqual('{{yb_variable}}')
    })
})
