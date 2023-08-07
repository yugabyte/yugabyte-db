/*
 * Created on Thu May 04 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { createRef } from "react";
import { HTMLSerializer } from "./HTMLSerializer";
import { IYBEditor, LoadPlugins, toggleBlock, toggleMark } from "../../plugins";
import { render } from "../../../../../test-utils";
import { MuiThemeProvider } from "@material-ui/core";
import { mainTheme } from '../../../../theme/mainTheme';
import { YBEditor } from "../../YBEditor";
import { fillAlertVariablesWithValue, findInvalidVariables, loadTemplateIntoEditor } from "../../../../features/alerts/TemplateComposer/composers/ComposerUtils";
import { Transforms } from "slate";
import { convertHTMLToText } from "../../transformers/HTMLToTextTransform";
import { EditableProps } from "slate-react/dist/components/editable";

jest.mock('react-i18next', () => ({
    // this mock makes sure any components using the translate hook can use it without a warning being shown
    useTranslation: () => {
        return {
            t: (str: any) => str,
            i18n: {
                changeLanguage: () => new Promise(() => { }),
            },
        };
    },
}));

interface YBEditorProps {
    editorProps?: EditableProps & { 'data-testid'?: string };
    loadPlugins?: LoadPlugins;
}

const setup = (props?: YBEditorProps) => {
    const editorRef = createRef<IYBEditor>();
    render(
        <MuiThemeProvider theme={mainTheme}>
            <YBEditor
                ref={editorRef}
                {...props}
            />
        </MuiThemeProvider>

    );
    return { editorRef, serializer: new HTMLSerializer(editorRef.current!) }
}

describe('HTML Serializer test', () => {
    it('shoud return empty paragraph element', () => {
        const { serializer } = setup();

        expect(serializer.serialize()).toEqual('<p align="left"></p>')
    })
    it('should serialize editor contents', () => {
        const { serializer, editorRef } = setup();
        loadTemplateIntoEditor('ybeditor test in progress', { customVariables: [], systemVariables: [] }, editorRef.current)
        expect(serializer.serialize()).toEqual('<p>ybeditor test in progress</p>')
    })
    it('should serialize multiple line contents', () => {
        const { serializer, editorRef } = setup();
        loadTemplateIntoEditor('ybeditor test in progress\nsecondline', { customVariables: [], systemVariables: [] }, editorRef.current)
        expect(serializer.serialize()).toEqual('<p>ybeditor test in progress</p><p>secondline</p>')
    })
    it('show support bold italics etc', () => {
        const { serializer, editorRef } = setup();
        loadTemplateIntoEditor('ybeditor This is some long text', { customVariables: [], systemVariables: [] }, editorRef.current);
        Transforms.select(editorRef.current!, {
            anchor: { path: [0, 0], offset: 8 },
            focus: { path: [0, 0], offset: 26 }
        })

        toggleMark(editorRef.current!, 'bold')
        toggleMark(editorRef.current!, 'italic')

        expect(serializer.serialize()).toEqual('<p>ybeditor<em><strong> This is some long</strong></em> text</p>')
        toggleMark(editorRef.current!, 'italic')

        expect(serializer.serialize()).toEqual('<p>ybeditor<strong> This is some long</strong> text</p>')
        expect(convertHTMLToText(serializer.serialize())).toEqual("ybeditor<strong> This is some long</strong> text")

        toggleMark(editorRef.current!, 'bold')
        expect(convertHTMLToText(serializer.serialize())).toEqual("ybeditor This is some long text")
    })

    it('should not render alert variables in alertPlugin is not enabled', () => {
        const { serializer, editorRef } = setup();
        loadTemplateIntoEditor("yb editor with system variable {{system}} and custom variable {{custom}}",
            {
                systemVariables: [{ name: 'system', description: 'test' }],
                customVariables: [{ name: 'custom', defaultValue: '1', possibleValues: ['1,2,3'] }]
            },
            editorRef.current
        );
        expect(serializer.serialize()).toEqual('<p>yb editor with system variable  and custom variable </p>')
    })

    it('should render alert variables properly', () => {
        const { serializer, editorRef } = setup({ loadPlugins: { alertVariablesPlugin: true } });
        const templateStr = "yb editor with system variable {{system}} and custom variable {{custom}}"
        const alertVariables = {
            systemVariables: [{ name: 'system', description: 'test' }],
            customVariables: [{ name: 'custom', defaultValue: '1', possibleValues: ['1,2,3'] }]
        }
        loadTemplateIntoEditor(templateStr,
            alertVariables,
            editorRef.current
        );
        expect(serializer.serialize()).toEqual('<p>yb editor with system variable <span type="alertVariable" variableType="System" variableName="system">{{system}}</span> and custom variable <span type="alertVariable" variableType="Custom" variableName="custom">{{custom}}</span></p>')

        let invalidVariables = findInvalidVariables(templateStr, alertVariables);
        expect(invalidVariables).toHaveLength(0)

        
        invalidVariables = findInvalidVariables(templateStr, { customVariables: [{ name: 'custom', defaultValue: 'test', possibleValues: ['1,2,3'] }], systemVariables: [] });
        expect(invalidVariables).toHaveLength(1);
        expect(invalidVariables[0]).toEqual('system')


        fillAlertVariablesWithValue(editorRef.current!, serializer.serialize())
        expect(editorRef.current?.children[0]['children'][1]['view']).toEqual('PREVIEW');

    })

    it('should render paragraph alignments properly', () => {
        const { serializer, editorRef } = setup({ loadPlugins: { alertVariablesPlugin: true } });
        const templateStr = "yb editor with system variable {{system}} and custom variable {{custom}}"
        const alertVariables = {
            systemVariables: [{ name: 'system', description: 'test' }],
            customVariables: [{ name: 'custom', defaultValue: '1', possibleValues: ['1,2,3'] }]
        }
        loadTemplateIntoEditor(templateStr,
            alertVariables,
            editorRef.current
        );
    
        toggleBlock(editorRef.current!, 'right');
        expect(serializer.serialize()).toEqual('<p align=\"right\">yb editor with system variable <span type=\"alertVariable\" variableType=\"System\" variableName=\"system\">{{system}}</span> and custom variable <span type=\"alertVariable\" variableType=\"Custom\" variableName=\"custom\">{{custom}}</span></p>');
        let plainText = convertHTMLToText(serializer.serialize())
        expect(plainText).toEqual("<p align=\"right\">yb editor with system variable {{system}} and custom variable {{custom}}</p>")


        toggleBlock(editorRef.current!, 'right'); //remove align right
        plainText = convertHTMLToText(serializer.serialize());
        expect(plainText).toEqual('yb editor with system variable {{system}} and custom variable {{custom}}')
    })

})
