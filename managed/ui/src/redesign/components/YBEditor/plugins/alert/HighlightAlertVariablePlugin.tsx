/*
 * Created on Mon Apr 17 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */


import React from 'react';

import clsx from 'clsx';
import { NodeEntry, Selection } from 'slate';
import { IYBSlatePlugin, SlateRenderElementProps, SlateRenderLeafProps } from '../IPlugin';
import { ALERT_VARIABLE_REGEX, nonActivePluginReturnType } from '../PluginUtils';

import { CustomText } from '../custom-types';

/**
 * styles for syntax highlighting
 */
import './HighlightAlertVariablePlugin.scss';

const PLUGIN_NAME = 'Highlight Alert Variable';

export const useHighlightAlertVariablePlugin: IYBSlatePlugin = ({ enabled, editor }) => {



    if (!enabled) {
        return { name: PLUGIN_NAME, ...nonActivePluginReturnType };
    }



    const renderElement = ({ attributes, children, element }: SlateRenderElementProps) => undefined;

    const renderLeaf = ({ attributes, children, leaf }: SlateRenderLeafProps) => {
        if (leaf.decoration?.alert) {
            return <span {...attributes} className={clsx('alertVariables', leaf.decoration?.alert.type)}>{children}</span>
        }
        return undefined;
    };

    const onKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => false;

    return {
        name: PLUGIN_NAME,
        renderElement,
        onKeyDown,
        isEnabled: () => enabled,
        renderLeaf,
        decorator,
        defaultComponents: []
    };
};

type AlertVariableSelection = Selection & { decoration: { alert: { type: string } } };

const decorator = (entry: NodeEntry<CustomText>): AlertVariableSelection[] => {
    const [node, path] = entry;

    const nodeText = node.text;

    if (!nodeText) return [];


    const variables:[string,number][] = findAlertVariables(nodeText);
    
    return variables.map((variable) => {
        const [text, index] = variable;
        return {
        anchor: {
            path,
            offset: index
        },
        focus: {
            path,
            offset: index + text.length
        },
        decoration: {
            alert: {
                type: 'alertVariable'
            }
        }
    }})
}

const findAlertVariables = (text: string):any => {
    const matches = text.match(ALERT_VARIABLE_REGEX);
    return matches ? matches.map((m) => [m.trim(), text.indexOf(m.trim())]) : [];
}
