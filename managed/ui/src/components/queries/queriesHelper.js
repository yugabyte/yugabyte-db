import { useState, useEffect } from 'react';
import { useQuery } from 'react-query';
import { fetchLiveQueries, fetchSlowQueries } from '../../actions/universe';

export const useLiveQueriesApi = ({ universeUUID }) => {
  const [ycqlQueries, setYCQLQueryRowData] = useState([]);
  const [ysqlQueries, setYSQLQueryRowData] = useState([]);
  const [errors, setErrors] = useState({});
  
  const handleQueryResponse = (response) => {
    const { error, data } = response;    
    if (!error) {
      const allErrors = {};
      if ('ysql' in data) {
        setYSQLQueryRowData(data.ysql.queries);
        allErrors.ysql = data.ysql.errorCount;
      }
      if ('ycql' in data) {
        setYCQLQueryRowData(data.ycql.queries);
        allErrors.ycql = data.ycql.errorCount;
      }
      setErrors(allErrors);
    } else {
      console.error(error);
      setErrors({ message: error });
    }
  };

  const { refetch, isFetching } = useQuery(
    ['getLiveQueries', universeUUID],
    () => fetchLiveQueries(universeUUID),
    {
      retry: false,
      refetchOnWindowFocus: true,
      onSuccess: handleQueryResponse
    }
  );

  return {
    ycqlQueries,
    ysqlQueries,    
    errors,
    loading: isFetching,
    getLiveQueries: refetch
  };
};

export const useSlowQueriesApi = ({ universeUUID }) => {
  const [ysqlQueries, setYSQLQueryRowData] = useState([]);
  const [errors, setErrors] = useState({});

  const handleQueryResponse = (response) => {
    const { data } = response;
    if (!data.error) {
      const allErrors = {};
      if ('ysql' in data) {
        setYSQLQueryRowData(data.ysql.queries);
        allErrors.ysql = data.ysql.errorCount;
      }      
      setErrors(allErrors);
    } else {
      setErrors({ message: data.error });
    }
  };

  const { refetch, isFetching } = useQuery(
    ['getSlowQueries', universeUUID],
    () => fetchSlowQueries(universeUUID),
    {
      retry: false,
      refetchOnWindowFocus: true,
      onSuccess: handleQueryResponse
    }
  );

  return {
    ysqlQueries,    
    errors,
    loading: isFetching,
    getSlowQueries: refetch
  };
};

const comparisonRegex = /(^([><]=?)(\d+))|(^(\d+|\*)\.\.(\d+|\*))/;
const timestampRegex = /(^([><]=?)((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(\+\d{2}:\d{2})?))|^((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(\+\d{2}:\d{2})?|\*)\.\.((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(\+\d{2}:\d{2})?|\*)/;

export const filterBySearchTokens = (arr, searchTokens, keyMap) => {
  return arr.filter((query) => {
    if (searchTokens.length) {
      return searchTokens.every((token) => {
        if (token.key) {
          const column = keyMap[token.label];
          if (column.type === 'number') {
            /**
             *  Test for comparison or range operator syntax similar to Github.
             *  See https://docs.github.com/en/free-pro-team@latest/github/searching-for-information-on-github/understanding-the-search-syntax
             *
             *  Query	 |  Example
             * --------------------------------------------------------------------------------------------------
             *   >n	   |  `Elapsed Time:>1000` matches all rows with an 'Elapsed Time' value of greater than 1000.
             *   >=n   |  `Elapsed Time:>=5` matches all rows with an 'Elapsed Time' of 5 or more.
             *   <n	   |  `Elapsed Time:<10000` matches all rows with an 'Elapsed Time' value of less than 10000.
             *   <=n	 |  `Elapsed Time:<=50` matches all rows with an 'Elapsed Time' of 50 or less.
             *   n..*  |  `Elapsed Time:10..*` matches all rows with an 'Elapsed Time' of 10 or more.
             *   *..n  |  `Elapsed Time:10..*` matches all rows with an 'Elapsed Time' of 10 or fewer.
             *   n..n  |  `Elapsed Time:10..50*` matches all rows with an 'Elapsed Time' between 10 and 50.
             */

            if (comparisonRegex.test(token.value)) {
              const match = comparisonRegex.exec(token.value);
              const operator = match[2];
              if (operator) {
                const limit = match[3];
                // eslint-disable-next-line no-new-func
                return Function(`return ${query[token.key]} ${operator} ${limit}`)();
              }
              const lowerRange = match[5];
              const upperRange = match[6];
              if (lowerRange === '*' && !Number.isNaN(parseFloat(upperRange))) {
                return query[column.value] <= parseFloat(upperRange);
              } else if (upperRange === '*' && !Number.isNaN(parseFloat(lowerRange))) {
                return query[column.value] <= parseFloat(upperRange);
              } else if (
                !Number.isNaN(parseFloat(lowerRange)) &&
                !Number.isNaN(parseFloat(upperRange))
              ) {
                return (
                  query[column.value] >= parseFloat(lowerRange) &&
                  query[column.value] <= parseFloat(upperRange)
                );
              }
            }
            return query[column.value] === parseFloat(token.value.trim());
          } else if (column.type === 'timestamp') {
            /**
             * Test for date-time comparisons similar to Github search syntax above.
             * Currently do not support YYYY-MM-DD comparisons and must include the time
             * YYYY-MM-DDTHH:MM:SS with optional timezone offset.
             */

            if (timestampRegex.test(token.value)) {
              const match = timestampRegex.exec(token.value);
              const operator = match[2];
              if (operator) {
                const timestampLimit = match[3];
                // eslint-disable-next-line no-new-func
                return Function(
                  `"use strict";return new Date("${
                    query[column.value]
                  }") ${operator} new Date("${timestampLimit}")`
                )();
              }
              const lowerTimeRange = match[11];
              const upperTimeRange = match[19];
              const queryTime = new Date(query[column.value]);
              if (upperTimeRange === '*' && lowerTimeRange !== '*') {
                return queryTime >= new Date(lowerTimeRange);
              } else if (lowerTimeRange === '*' && upperTimeRange !== '*') {
                return queryTime <= new Date(upperTimeRange);
              } else if (lowerTimeRange !== '*' && upperTimeRange !== '*') {
                return (
                  queryTime >= new Date(lowerTimeRange) && queryTime <= new Date(upperTimeRange)
                );
              }
            }
            return query[column.value].includes(token.value.trim());
          } else {
            return column.value in query && query[column.value].includes(token.value);
          }
        } else {
          // Search through all properties for token value
          return Object.values(query).some((val) => String(val).includes(token.value));
        }
      });
    }
    return true;
  });
};
