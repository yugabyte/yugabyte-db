import React, { FC, useRef } from 'react';
import clsx from 'clsx';
import { Box, makeStyles } from '@material-ui/core';
import '@app/assets/fonts/Menlo-Regular.woff';
import { YBButton } from '../YBButton/YBButton';
import { AlertVariant } from '..';
import { useToast } from '@app/helpers';
import { useTranslation } from 'react-i18next';

export interface CodeBlockProps {
  analyticsEventOnCopy?: string;
  showCopyButton?: boolean;
  multiBlock?: boolean;
  codeClassName?: string;
  preClassName?: string;
  text: string | string[] | React.ReactElement | React.ReactElement[];
}

const useStyles = makeStyles((theme) => ({
  block: {
    background: theme.palette.info[400],
    borderRadius: theme.shape.borderRadius,
    padding: theme.spacing(1, 1, 1, 1.5),
    color: theme.palette.grey[800],
    margin: theme.spacing(0, 0, 1, 0),
    border: `1px solid ${theme.palette.grey[300]}`,
    position: 'relative',
    overflowX: 'hidden',

    '&:hover': {
      overflowX: 'auto'
    }
  },

  code: {
    display: 'block',
    position: 'relative',
    fontFamily: 'Menlo-Regular, Courier, monospace',
    fontSize: 13,
    lineHeight: '17px',
    borderRadius: theme.spacing(0.5)
  },
  copyButton: {
    position: 'absolute',
    right: 0,
    top: 0,
    backgroundColor: theme.palette.info[200],
    '& .MuiButton-label': {
      textTransform: 'uppercase',
      fontSize: 11.5
    }
  },
  inlineButton: {
    top: theme.spacing(0.5),
    right: theme.spacing(0.75),
    display: 'none'
  },
  hoverBlock: {
    padding: theme.spacing(0.5, 0.75),

    '&:hover': {
      background: theme.palette.primary[200]
    },

    '&:hover .yb-copy-btn': {
      display: 'block'
    }
  },
  innerBlock: {
    fontFamily: 'Menlo-Regular, Courier, monospace',
    whiteSpace: 'pre-wrap',
    margin: 0,
    lineHeight: 2.7,
    paddingRight: theme.spacing(8)
  }
}));

export const YBCodeBlock: FC<CodeBlockProps> = ({
  showCopyButton,  
  multiBlock,
  text,
  codeClassName,
  preClassName
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const ref = useRef<HTMLPreElement>(null);

  const { addToast } = useToast();

  const copy = async (value: string) => {
    await navigator.clipboard.writeText(value);
    addToast(AlertVariant.Success, t('common.copyCodeSuccess'));
  };

  const copyBlock = () => copy(ref?.current?.innerText ?? '');

  const copyLineBlock = (ev: React.MouseEvent) => copy((ev?.currentTarget?.previousSibling as HTMLElement).innerText);

  return (
    <>
      <Box className={classes.block}>
        {multiBlock && Array.isArray(text) ? (
          text.map((val: string | React.ReactElement, index: number) => (
            <code className={clsx(classes.code, classes.hoverBlock)} key={`code-block-line-${String(index + 1)}`}>
              <pre className={classes.innerBlock}>{val}</pre>
              {showCopyButton && (
                <YBButton
                  variant="ghost"
                  className={clsx(classes.copyButton, classes.inlineButton, 'yb-copy-btn')}
                  onClick={copyLineBlock}
                >
                  {t('common.copy')}
                </YBButton>
              )}
            </code>
          ))
        ) : (
          <code className={clsx(classes.code, codeClassName)}>
            <pre className={clsx(classes.innerBlock, preClassName)} ref={ref}>
              {text}
            </pre>
            {showCopyButton && (
              <YBButton variant="ghost" className={classes.copyButton} onClick={copyBlock}>
                {t('common.copy')}
              </YBButton>
            )}
          </code>
        )}
      </Box>
    </>
  );
};
