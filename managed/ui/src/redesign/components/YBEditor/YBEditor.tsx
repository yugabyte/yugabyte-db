/*
 * Created on Thu Mar 02 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { MutableRefObject, useImperativeHandle, useMemo } from 'react';
import clsx from 'clsx';
import { createEditor, Descendant } from 'slate';
import { Slate, Editable, withReact } from 'slate-react';
import { EditableProps } from 'slate-react/dist/components/editable';
import { withHistory } from 'slate-history';
import { makeStyles } from '@material-ui/core';
import { IYBEditor } from './plugins/custom-types';
import { LoadPlugins, useEditorPlugin } from './plugins/PluginManager';
import { DefaultElement } from './plugins/PluginUtils';
interface YBEditorProps {
  initialValue?: Descendant[];
  setVal?: (val: Descendant[]) => void;
  editorProps?: EditableProps & {'data-testid'?: string};
  loadPlugins?: LoadPlugins;
  ref: MutableRefObject<IYBEditor>;
  editorClassname?: any,
  showLineNumbers?: boolean;
  onEditorKeyDown?: (e: React.KeyboardEvent<HTMLDivElement>) => void;
}

const useStyles = makeStyles((theme) => ({
  root: {
    background: theme.palette.common.white,
    position: 'relative'

  },
  rootSingleLine: {
    borderRadius: theme.spacing(1),
    overflowX: 'auto'
  },
  editable: {
    height: '380px',
    padding: theme.spacing(2),
    overflowY: 'auto'
  },
  singleLine: {
    height: theme.spacing(4),
    padding: theme.spacing(0.5),
    overflowY: 'hidden',
    '&::-webkit-scrollbar': {
      display: 'none'
    },
    scrollbarWidth: 'none'
  },
  // Display line number like in IDE
  lineNumberBackground: {
    position: 'absolute',
    height: '100%',
    left: 0,
    top: 0,
    width: theme.spacing(5.25),
    background: theme.palette.ybacolors.ybBorderGray
  },
  showLineNumbers: {
    counterReset: 'line 0',
    '&> *': {
      counterIncrement: 'line',
      "&::before": {
        content: 'counter(line)',
        marginRight: theme.spacing(1),
        height: theme.spacing(3),
        width: theme.spacing(5.25),
        textAlign: 'center',
        display: 'inline-block',
        marginLeft: theme.spacing(-2.25)
      }
    }
  }
}));

export const YBEditor = React.forwardRef<IYBEditor, React.PropsWithChildren<YBEditorProps>>(
  (
    {
      editorProps,
      setVal,
      loadPlugins = {},
      initialValue = [DefaultElement],
      editorClassname = {},
      showLineNumbers = false,
      onEditorKeyDown
    },
    forwardRef
  ) => {
    const editor = useMemo(() => withHistory(withReact(createEditor())), []);

    //only basic plugins are enabled by default
    const enabledPlugins: LoadPlugins = {
      basic: true,
      alertVariablesPlugin: false,
      singleLine: false,
      ...loadPlugins
    };

    useImperativeHandle(forwardRef, () => editor, []);

    const { renderElement, onKeyDown, renderLeaf, getDefaultComponents, getDecorators } = useEditorPlugin(
      editor,
      enabledPlugins
    );
    const classes = useStyles();

    return (
      <div className={clsx(classes.root, { [classes.rootSingleLine]: enabledPlugins.singleLine })}>
        {/* Show Line numbers to the left */}
        {
          showLineNumbers && (<div className={classes.lineNumberBackground} />)
        }
        <Slate editor={editor} value={initialValue} onChange={(val) => setVal?.(val)}>
          <Editable
            className={clsx(classes.editable, { [classes.singleLine]: enabledPlugins.singleLine, [classes.showLineNumbers]: showLineNumbers }, editorClassname)}
            renderElement={renderElement}
            onKeyDown={e => {
              onEditorKeyDown && onEditorKeyDown(e);
              return onKeyDown(e);
            }}
            renderLeaf={renderLeaf}
            spellCheck
            autoFocus
            style={enabledPlugins.singleLine ? { whiteSpace: 'pre' } : {}}
            decorate={getDecorators as any}
            {...editorProps}
          />
          {/* inject default components from the plugins into the dom */}
          {getDefaultComponents().map((components) => {
            if (!components) return;
            return components.map((comp: Function, ind: number) => (
              <React.Fragment key={ind}>{comp()}</React.Fragment>
            ));
          })}
        </Slate>
      </div>
    );
  }
);
