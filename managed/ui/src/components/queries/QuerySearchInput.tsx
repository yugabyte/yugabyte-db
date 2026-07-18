import React, { FC, useEffect, useLayoutEffect, useRef, useState } from 'react';

const TAB_KEY = 'Tab';
const ENTER_KEY = 'Enter';
const DOWN_ARROW_KEY = 'ArrowDown';
const UP_ARROW_KEY = 'ArrowUp';

export interface SearchTokensType {
  key?: string;
  label?: string;
  value: string;
}

export interface QuerySearchInputProps {
  id: string;
  placeholder: string;
  searchTerms: SearchTokensType[];
  // key map of available columns to display
  columns: Record<string, { value: string, type: string }>;
  onSearch: Function;
  onClear: Function;
  onSubmitSearchTerms: Function;
}

export const QuerySearchInput: FC<QuerySearchInputProps> = ({
  columns,
  id,
  placeholder,
  searchTerms,
  onSearch = () => {},
  onClear = () => {},
  onSubmitSearchTerms
}) => {
  const [showAutoComplete, setShowAutoComplete] = useState(false);
  const [autoCompleteResults, setAutoCompleteResults] = useState(Object.keys(columns));
  const [selectedAutoCompleteEntry, setSelectedAutoCompleteEntry] = useState<number | null>(null);
  const querySearchInput = useRef<HTMLInputElement>(null);
  const [searchText, setSearchText] = useState<string>('');
  const [searchDropdownLeftPx, setSearchDropdownLeft] = useState(0);

  // Sets search token based on user input, triggered from ENTER or TAB
  const submitSearchTerm = () => {
    const separatorIndex = searchText.indexOf(':');
    if (separatorIndex > -1 && searchText.substring(0, separatorIndex) in columns) {
      onSubmitSearchTerms([
        ...searchTerms,
        {
          key: columns[searchText.substring(0, separatorIndex)].value,
          label: searchText.substring(0, separatorIndex),
          value: searchText.substring(separatorIndex + 1)
        }
      ]);
    } else {
      onSubmitSearchTerms([...searchTerms, { value: searchText }]);
      onSearch(searchText);
    }
    setSearchText('');
    setAutoCompleteResults(Object.keys(columns));
    setShowAutoComplete(true);
    setSelectedAutoCompleteEntry(null);
    if (querySearchInput.current) {
      querySearchInput.current.focus();
    }
  };

  const handleOnChange = (ev: React.ChangeEvent<HTMLInputElement>) => {
    const searchText = ev.target.value;
    if (!searchText.trim()) {
      setAutoCompleteResults(Object.keys(columns));
      setShowAutoComplete(false);
    } else {
      const keyMatches = Object.keys(columns).filter((x) =>
        x.toLowerCase().includes(searchText.toLowerCase())
      );
      if (keyMatches.length) {
        setAutoCompleteResults(keyMatches);
        setShowAutoComplete(true);
      } else {
        setShowAutoComplete(false);
      }
    }
    setSearchText(searchText);
  };

  const handleKeyPress = (ev: React.KeyboardEvent) => {
    if (ev.key === TAB_KEY) {
      // TAB should close autocomplete if open,
      // otherwise set search token to search text
      if (showAutoComplete) {
        setShowAutoComplete(false);
        setSelectedAutoCompleteEntry(null);
        if (querySearchInput.current) {
          querySearchInput.current.focus();
        }

        ev.preventDefault();
      } else if (searchText) {
        submitSearchTerm();
      }
    } else if (ev.key === ENTER_KEY) {
      // ENTER should add column from autocomplete if open,
      // otherwise set search token to search text
      if (showAutoComplete && selectedAutoCompleteEntry !== null) {
        setSearchText(`${autoCompleteResults[selectedAutoCompleteEntry]}:`);
        setShowAutoComplete(false);
        setSelectedAutoCompleteEntry(null);
        setAutoCompleteResults(Object.keys(columns));
        if (querySearchInput.current) {
          querySearchInput.current.focus();
        }
      } else if (searchText) {
        submitSearchTerm();
      }
    } else if (ev.key === DOWN_ARROW_KEY) {
      // DOWN_ARROW should highlight the first entry in autocomplete results,
      // or move to the next element in array, looping back to top if needed
      if (showAutoComplete) {
        if (
          selectedAutoCompleteEntry === null ||
          selectedAutoCompleteEntry === autoCompleteResults.length - 1
        ) {
          setSelectedAutoCompleteEntry(0);
        } else {
          setSelectedAutoCompleteEntry(selectedAutoCompleteEntry + 1);
        }
        ev.preventDefault();
      }
    } else if (ev.key === UP_ARROW_KEY) {
      // UP_ARROW should highlight the last entry in autocomplete results,
      // or move to the previous element in array, looping back to bottom if needed
      if (showAutoComplete) {
        if (selectedAutoCompleteEntry === null || selectedAutoCompleteEntry === 0) {
          setSelectedAutoCompleteEntry(autoCompleteResults.length - 1);
        } else {
          setSelectedAutoCompleteEntry(selectedAutoCompleteEntry - 1);
        }
        ev.preventDefault();
      }
    }
  };

  // When user clicks autosuggested column name in dropdown
  const handleTokenClick: React.MouseEventHandler = (e) => {
    setSearchText(`${(e.target as HTMLElement).innerText}:`);
    if (querySearchInput.current) {
      querySearchInput.current.focus();
    }
  };

  useEffect(() => {
    const searchDropdownHandler: EventListener = (ev) => {
      const searchBarEl = document.getElementById(id);
      if (searchBarEl && !searchBarEl.contains((ev.target as HTMLElement))) {
        setShowAutoComplete(false);
        setSelectedAutoCompleteEntry(null);
      }
    };
    document.addEventListener('click', searchDropdownHandler);

    return () => {
      document.removeEventListener('click', searchDropdownHandler);
    };
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  // Gets the location of querySearchInput element and sets left pixels
  useLayoutEffect(() => {
    if (querySearchInput.current) {
      const element = document.getElementById(id);
      if (element && querySearchInput.current.getBoundingClientRect().left > 0) {
        setSearchDropdownLeft(
          querySearchInput.current.getBoundingClientRect().left -
            element.getBoundingClientRect().left -
            15
        );
      } else {
        setSearchDropdownLeft(0);
      }
    }
  }, [querySearchInput, searchTerms]); // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <div id={id} className="search-bar-container">
      <div className="search-bar">
        {searchTerms.map((token, idx) => (
          <span className="chip" key={`token-${token.key}-${idx}`}>
            {token.label && <span className="key">{token.label}: </span>}
            <span className="value">{token.value}</span>
            <i
              className="fa fa-times-circle remove-chip"
              onClick={() => {
                const newTokens = [...searchTerms];
                newTokens.splice(idx, 1);
                onSubmitSearchTerms(newTokens);
                onClear();
              }}
            />
          </span>
        ))}
        <input
          placeholder={placeholder}
          value={searchText}
          ref={querySearchInput}
          onChange={handleOnChange}
          onKeyDown={handleKeyPress}
          onFocus={() => setShowAutoComplete(true)}
        />
        {searchText && (
          <i
            className="fa fa-times"
            onClick={() => {
              setSearchText('');
              setAutoCompleteResults(Object.keys(columns));
              onClear();
            }}
          />
        )}
      </div>
      {showAutoComplete && (
        <div
          className="autocomplete-wrapper"
          onClick={handleTokenClick}
          style={{
            left: `${searchDropdownLeftPx}px`
          }}
        >
          <ul>
            {autoCompleteResults.map((key, index) => (
              <li
                data-col-key={columns[key].value}
                key={columns[key].value}
                className={selectedAutoCompleteEntry === index ? 'active' : ''}
              >
                {key}
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  );
};
