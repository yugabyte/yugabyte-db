import { useState } from 'react';
import { Chip, Input, makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';

import { generateLowerCaseAlphanumericId } from '../../../components/configRedesign/providerRedesign/forms/utils';

interface YBSmartSearchBarProps {
  searchTokens: SearchToken[];
  onSearchTokensChange: (searchTokens: SearchToken[]) => void;
  recognizedModifiers: string[];

  className?: string;
  dataTestIdPrefix?: string;
  placeholder?: string;
  autoExpandMinWidth?: number;
}

export interface SearchToken {
  id: string;
  modifier: string | null;
  value: string;
}

const useStyles = makeStyles((theme) => ({
  searchBarContainer: {
    display: 'flex',
    alignItems: 'center',
    flexWrap: 'wrap',
    rowGap: theme.spacing(0.5),

    width: (props: YBSmartSearchBarProps) => (props.autoExpandMinWidth ? 'fit-content' : '100%'),
    minWidth: (props: YBSmartSearchBarProps) => props.autoExpandMinWidth,
    padding: theme.spacing(0.5),

    border: `1px solid ${theme.palette.ybacolors.ybGray}`,
    '&:hover': {
      borderColor: theme.palette.ybacolors.inputBackground
    },
    '&:focus-within': {
      borderColor: theme.palette.ybacolors.ybOrangeFocus,
      boxShadow: theme.shape.ybaShadows.inputBoxShadow
    },
    borderRadius: theme.shape.borderRadius
  },
  input: {
    flexGrow: 1,

    height: 'unset',

    border: 'none',
    '&.Mui-focused': {
      boxShadow: 'none'
    }
  },
  chip: {
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    maxWidth: '100%',

    borderRadius: theme.spacing(1),

    '&:not(:last-child)': {
      marginRight: theme.spacing(0.5)
    }
  }
}));

const TAB_KEY = 'Tab';
const ENTER_KEY = 'Enter';
export const YBSmartSearchBar = ({
  searchTokens,
  onSearchTokensChange,
  recognizedModifiers,
  placeholder,
  dataTestIdPrefix,
  autoExpandMinWidth,
  className
}: YBSmartSearchBarProps) => {
  const [inputText, setInputText] = useState<string>('');
  const classes = useStyles({
    searchTokens,
    onSearchTokensChange,
    recognizedModifiers,
    placeholder,
    dataTestIdPrefix,
    autoExpandMinWidth,
    className
  });

  const handleInputChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    setInputText(event.target.value);
  };

  const handleInputKeyDown = (event: React.KeyboardEvent<HTMLInputElement>) => {
    switch (event.key) {
      case TAB_KEY: {
        event.preventDefault();
        const trimmedInput = inputText.trim();
        if (trimmedInput !== '') {
          const splitIndex = trimmedInput.indexOf(':');
          const [modifier, value] =
            splitIndex > 0 && recognizedModifiers.includes(trimmedInput.substring(0, splitIndex))
              ? [trimmedInput.substring(0, splitIndex), trimmedInput.substring(splitIndex + 1)]
              : [null, trimmedInput];

          const newTokens = [
            ...searchTokens,
            { id: generateLowerCaseAlphanumericId(), modifier, value: value.trim() }
          ];
          setInputText('');
          onSearchTokensChange(newTokens);
        }
        return;
      }
      case ENTER_KEY:
        event.preventDefault();
        return;
      default:
        return;
    }
  };

  const handleDeleteToken = (tokenToDelete: SearchToken) => () => {
    const newTokens = searchTokens.filter((token) => token.id !== tokenToDelete.id);
    onSearchTokensChange(newTokens);
  };

  const dataTestIdCore = dataTestIdPrefix
    ? `${dataTestIdPrefix}-YBSmartSearchBar`
    : 'YBSmartSearchBar';
  return (
    <div className={clsx(classes.searchBarContainer, className)}>
      {searchTokens.map((token) => (
        <Chip
          className={classes.chip}
          key={token.id}
          size="small"
          label={getTokenLabel(token)}
          onDelete={handleDeleteToken(token)}
        />
      ))}
      <Input
        value={inputText}
        onChange={handleInputChange}
        onKeyDown={handleInputKeyDown}
        disableUnderline
        placeholder={placeholder}
        className={classes.input}
        inputProps={{
          'data-testid': `${dataTestIdCore}-input`
        }}
      />
    </div>
  );
};

const getTokenLabel = (token: SearchToken) => {
  return token.modifier ? (
    <Typography variant="body2">
      <b> {`${token.modifier}:`}</b>
      {token.value}
    </Typography>
  ) : (
    <Typography variant="body2">{token.value}</Typography>
  );
};
