/*
 * Created on Mon May 08 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { TextToHTMLTransform } from "./TextToHTMLTransform"


describe('text to html transform tests', () => {

    it('should convert simple text to paragraph', () => {
        let html = TextToHTMLTransform('this is ybeditor test script', { customVariables: [], systemVariables: [] })
        expect(html).toEqual('<p>this is ybeditor test script</p>')
    })

    it('should return all the markup tags as usual', () => {
        let html = TextToHTMLTransform('this is <strong><em>ybeditor</em> test</strong> script', { customVariables: [], systemVariables: [] })
        expect(html).toEqual('<p>this is <strong><em>ybeditor</em> test</strong> script</p>')
    })

    it('should replace alert_variables with html tags', () => {
        let html = TextToHTMLTransform('this is {{alert_variable}} script', { customVariables: [], systemVariables: [{ name: 'alert_variable', 'description': 'test' }] })
        expect(html).toEqual('<p>this is <span variableType="System" type="alertVariable"  variableName="alert_variable" >{{alert_variable}}</span> script</p>')


        html = TextToHTMLTransform('this is {{custom}} script {{system}}', { customVariables: [{ 'name': 'custom', 'possibleValues': ['test', 'test1'], 'defaultValue': 'test' }], systemVariables: [{ name: 'system', 'description': 'test' }] })
        expect(html).toEqual('<p>this is <span variableType="Custom" type="alertVariable"  variableName="custom" >{{custom}}</span> script <span variableType="System" type="alertVariable"  variableName="system" >{{system}}</span></p>')
    })

    it('should support multiline text', () => {
        let html = TextToHTMLTransform('This is some text\n{{system}}\n{{custom}}\n', { customVariables: [{ 'name': 'custom', 'possibleValues': ['test', 'test1'], 'defaultValue': 'test' }], systemVariables: [{ name: 'system', 'description': 'test' }] })
        expect(html).toEqual('<p>This is some text</p><p><span variableType="System" type="alertVariable"  variableName="system" >{{system}}</span></p><p><span variableType="Custom" type="alertVariable"  variableName="custom" >{{custom}}</span></p><p></p>')
    })

    it('should render paragraph tag with align attribute', () => {
        let html = TextToHTMLTransform('<p align="right">this is ybeditor test script</p>\nnew line', { customVariables: [], systemVariables: [] })
        expect(html).toEqual('<p align="right">this is ybeditor test script</p><p>new line</p>')
    })
})
