import { useState } from 'react';
import { Pagination } from 'react-bootstrap';
import clsx from 'clsx';

const nav = {
  FIRST: 'first',
  LAST: 'last',
  PREV: 'prev',
  NEXT: 'next'
};
const side = {
  LEFT: 'left',
  RIGHT: 'right'
};
const firstPage = 1;

// Pass activePage as a prop to use YBPagination as controlled pagination
const YBPagination = (props) => {
  const [activePage, setActive] = useState(firstPage);
  const { numPages = 1, onChange = () => {}, siblingCount = 1 } = props;

  // Restrict curActivePage to be a valid page number [firstPage, numPages]
  const curActivePage = props.activePage
    ? Math.min(Math.max(props.activePage, firstPage), numPages)
    : activePage;
  const numHiddenLeft = firstPage + 2 + siblingCount - curActivePage;
  const numHiddenRight = curActivePage - (numPages - 2 - siblingCount);
  const siblingLeftBound = curActivePage - siblingCount;
  const siblingRightBound = curActivePage + siblingCount;

  const isSibling = (num) =>
    curActivePage - siblingCount <= num && num <= curActivePage + siblingCount;
  const isBoundary = (num) => num === firstPage || num === numPages;
  const showEllipsis = (curSide) => {
    // Call with side.LEFT or side.RIGHT (defaulting to side.RIGHT)
    // Returns true if skipping more than 1 page marker on the side of interest
    return curSide === side.LEFT
      ? curActivePage - siblingCount - firstPage - 1 > 1
      : numPages - (curActivePage + siblingCount + 1) > 1;
  };

  const handlePageChange = (value) => {
    let newPageNum = 1;
    switch (value) {
      case nav.FIRST:
        newPageNum = 1;
        break;
      case nav.LAST:
        newPageNum = numPages;
        break;
      case nav.PREV:
        newPageNum = Math.max(curActivePage - 1, firstPage);
        break;
      case nav.NEXT:
        newPageNum = Math.min(curActivePage + 1, numPages);
        break;
      default:
        newPageNum = isNaN(value) ? 1 : Number(value);
        break;
    }
    onChange(newPageNum);
    if (!props.activePage) {
      setActive(newPageNum);
    }
  };

  // Maintain a fixed number of markers
  // - Boundary page markers (First and last page)
  // - Ellipsis / Second and second last page
  // - Active page and its siblings
  // - If some of the above markers are hidden on either side:
  // -- Display an equal number of markers on the opposite side to maintain marker count
  const items = [];
  for (let num = firstPage; num <= numPages; num++) {
    if (
      isBoundary(num) ||
      isSibling(num) ||
      (num === firstPage + 1 && !showEllipsis(side.LEFT)) ||
      (num === numPages - 1 && !showEllipsis(side.RIGHT)) ||
      (numHiddenLeft > 0 && num <= siblingRightBound + numHiddenLeft) ||
      (numHiddenRight > 0 && num >= siblingLeftBound - numHiddenRight)
    ) {
      items.push(
        <Pagination.Item
          key={num}
          active={num === curActivePage}
          onClick={() => handlePageChange(num)}
        >
          {num}
        </Pagination.Item>
      );
    } else if (
      (num === firstPage + 1 && showEllipsis(side.LEFT)) ||
      (num === numPages - 1 && showEllipsis(side.RIGHT))
    ) {
      items.push(<Pagination.Ellipsis />);
    }
  }
  return (
    <Pagination className={clsx(props.className && props.className)}>
      <Pagination.Prev
        onClick={() => handlePageChange(nav.PREV)}
        disabled={curActivePage === firstPage}
      />
      {items}
      <Pagination.Next
        onClick={() => handlePageChange(nav.NEXT)}
        disabled={curActivePage === numPages}
      />
    </Pagination>
  );
};

export default YBPagination;
