/*
 * Created on Thu Mar 02 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { MutableRefObject, useCallback, useImperativeHandle, useMemo } from 'react';
import clsx from 'clsx';
import { createEditor, Descendant } from 'slate';
import { Slate, Editable, withReact } from 'slate-react';
import { EditableProps } from 'slate-react/dist/components/editable';
import { withHistory } from 'slate-history';
import { Grid, makeStyles } from '@material-ui/core';
import { IYBEditor, TextDecorators } from './plugins/custom-types';
import { LoadPlugins, useEditorPlugin } from './plugins/PluginManager';
import { DefaultElement, toggleMark, toggleBlock } from './plugins/PluginUtils';

//icons

import { ReactComponent as Italic } from './icons/italic.svg';
import { ReactComponent as Bold } from './icons/bold.svg';
import { ReactComponent as Underline } from './icons/underline.svg';
import { ReactComponent as Strikethrough } from './icons/strikethrough.svg';
import { FormatAlignCenter, FormatAlignLeft, FormatAlignRight } from '@material-ui/icons';

const ToolbarMarkIcons: Record<TextDecorators, { icon: React.ReactChild }> = {
  italic: {
    icon: <Italic />
  },
  bold: {
    icon: <Bold />
  },
  underline: {
    icon: <Underline className="big" />
  },
  strikethrough: {
    icon: <Strikethrough className="big" />
  }
};

const ToolbarBlockIcons: Record<string, { icon: React.ReactElement; fn: Function }> = {
  alignLeft: {
    icon: <FormatAlignLeft className="medium" />,
    fn: (editor: IYBEditor) => toggleBlock(editor, 'left')
  },
  alignCenter: {
    icon: <FormatAlignCenter className="medium" />,
    fn: (editor: IYBEditor) => toggleBlock(editor, 'center')
  },
  alignRight: {
    icon: <FormatAlignRight className="medium" />,
    fn: (editor: IYBEditor) => toggleBlock(editor, 'right')
  }
};

interface YBEditorProps {
  initialValue?: Descendant[];
  setVal?: (val: Descendant[]) => void;
  editorProps?: EditableProps;
  showToolbar?: boolean;
  singleLine?: boolean;
  loadPlugins?: LoadPlugins;
  moreToolbar?: (editor: IYBEditor) => JSX.Element;
  ref: MutableRefObject<IYBEditor>;
}

const useStyles = makeStyles((theme) => ({
  root: {
    background: theme.palette.common.white
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
  toolbarRoot: {
    height: theme.spacing(5.25),
    display: 'flex',
    justifyContent: 'space-between',
    background: '#FAFBFC',
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    padding: `0 ${theme.spacing(2)}px`
  },
  formatIcons: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),
    '& > svg': {
      width: theme.spacing(3),
      height: theme.spacing(1.75),
      '&.big': {
        height: theme.spacing(3)
      },
      '&.medium': {
        height: theme.spacing(2.25)
      }
    }
  }
}));

export const YBEditor = React.forwardRef<IYBEditor, React.PropsWithChildren<YBEditorProps>>(
  (
    {
      showToolbar = false,
      editorProps,
      setVal,
      loadPlugins = {},
      moreToolbar,
      initialValue = [DefaultElement]
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

    const { renderElement, onKeyDown, renderLeaf, getDefaultComponents } = useEditorPlugin(
      editor,
      enabledPlugins
    );
    const classes = useStyles();

    let Toolbar = useCallback(() => {
      if (!showToolbar) {
        return null;
      }
      return (
        <Grid className={classes.toolbarRoot} container alignItems="center">
          <Grid item className={classes.formatIcons}>
            {Object.keys(ToolbarMarkIcons).map((ic) =>
              React.cloneElement(ToolbarMarkIcons[ic].icon, {
                key: ic,
                onClick: (e: React.MouseEvent) => {
                  e.preventDefault();
                  toggleMark(editor, ic as TextDecorators);
                }
              })
            )}
            {Object.keys(ToolbarBlockIcons).map((ic) =>
              React.cloneElement(ToolbarBlockIcons[ic].icon, {
                key: ic,
                onClick: (e: React.MouseEvent) => {
                  e.preventDefault();
                  ToolbarBlockIcons[ic].fn(editor);
                }
              })
            )}
          </Grid>
          <Grid item>{moreToolbar && moreToolbar(editor)}</Grid>
        </Grid>
      );
    }, [showToolbar, moreToolbar]);

    return (
      <div className={clsx(classes.root, { [classes.rootSingleLine]: enabledPlugins.singleLine })}>
        {Toolbar()}
        <Slate editor={editor} value={initialValue} onChange={(val) => setVal?.(val)}>
          <Editable
            className={clsx(classes.editable, { [classes.singleLine]: enabledPlugins.singleLine })}
            renderElement={renderElement}
            onKeyDown={onKeyDown}
            renderLeaf={renderLeaf}
            spellCheck
            autoFocus
            style={enabledPlugins.singleLine ? { whiteSpace: 'pre' } : {}}
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
