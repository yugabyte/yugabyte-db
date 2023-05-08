/*
 * Created on Thu May 04 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { createRef } from 'react';
import { YBEditor } from './YBEditor';
import { render } from '../../../test-utils';
import { IYBEditor, IYBSlatePlugin, LoadPlugins, clearEditor, isEditorEmpty } from './plugins';
import { MuiThemeProvider } from '@material-ui/core';
import { mainTheme } from '../../theme/mainTheme';
import { EditableProps } from 'slate-react/dist/components/editable';
import { loadTemplateIntoEditor } from '../../features/alerts/TemplateComposer/composers/ComposerUtils';
import { convertHTMLToText } from './transformers/HTMLToTextTransform';
import { HTMLSerializer } from './serializers';

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
    const component = render(
        <MuiThemeProvider theme={mainTheme}>
            <YBEditor
                ref={editorRef}
                {...props}
            />
        </MuiThemeProvider>

    );
    return { component, editorRef };
};

describe('YBEditor render test', () => {

    it('should render', () => {
        const { component, editorRef } = setup({ editorProps: { 'data-testid': 'ybeditor' } });
        expect(component.getByTestId('ybeditor')).toBeInTheDocument();
        expect(editorRef.current?.children).toHaveLength(1)
    });

    it('should load all plugins', () => {
        const { editorRef } = setup({ editorProps: { 'data-testid': 'ybeditor' }, loadPlugins: { alertVariablesPlugin: true, basic: true, defaultPlugin: true, jsonPlugin: true, singleLine: true } });
        expect(editorRef.current?.['pluginsList']).toHaveLength(5)
        const basicPlugin = editorRef.current?.['pluginsList'].find((e: IYBSlatePlugin) => e.name === 'Basic')
        expect(basicPlugin.isEnabled()).toBeTruthy()
    })

    it('should load only selected plugins', () => {
        const { editorRef } = setup({ editorProps: { 'data-testid': 'ybeditor' }, loadPlugins: { singleLine: true, basic: false } });
        expect(editorRef.current?.['pluginsList']).toHaveLength(2) //default plugin is enabled by default. so the count is 2
        const basicPlugin = editorRef.current?.['pluginsList'].find((e: IYBSlatePlugin) => e.name === 'Basic')
        expect(basicPlugin).toBeUndefined();
        const singLinePlugin = editorRef.current?.['pluginsList'].find((e: IYBSlatePlugin) => e.name === 'Single Line Plugin')
        expect(singLinePlugin.isEnabled()).toBeTruthy();
    })

    it('should load contents', () => {
        const { editorRef } = setup();
        expect(isEditorEmpty(editorRef.current)).toBeTruthy()

        loadTemplateIntoEditor('sample editor test', { systemVariables: [], customVariables: [] }, editorRef.current)
        expect(isEditorEmpty(editorRef.current)).toBeFalsy();
        expect(editorRef.current?.children).toHaveLength(1);
        expect(editorRef.current?.children[0]['type']).toEqual('paragraph')
        expect(editorRef.current?.children[0]['children'][0].text).toEqual('sample editor test')
        const html = new HTMLSerializer(editorRef.current!).serialize()
        expect(convertHTMLToText(html)).toEqual('sample editor test')

        clearEditor(editorRef.current!);
        const emptyHtml = new HTMLSerializer(editorRef.current!).serialize()
        expect(convertHTMLToText(emptyHtml)).toEqual('')
    })
});
