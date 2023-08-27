import { render, screen } from '../../../test-utils';
import { within } from '@testing-library/dom';
import userEvent from '@testing-library/user-event';
import YBPagination from './YBPagination';

describe('YBPagination render default props', () => {
  beforeEach(() => {
    render(<YBPagination />);
  });
  it('renders the pagination button list with positive length', () => {
    const paginationList = screen.getByRole('list');
    const { getAllByRole } = within(paginationList);
    const paginationButtons = getAllByRole('listitem');
    expect(paginationList).toBeInTheDocument();
    expect(paginationButtons.length).not.toEqual(0);
  });
  it('renders the prev button', () => {
    expect(screen.getByRole('listitem', { name: /prev/i })).toBeInTheDocument();
  });
  it('renders the next button', () => {
    expect(screen.getByRole('listitem', { name: /next/i })).toBeInTheDocument();
  });
});

describe('YBPagination render with props (numPages)', () => {
  it('renders the pagination button list with positive length', () => {
    render(<YBPagination numPages={20} />);

    const paginationList = screen.getByRole('list');
    const { getAllByRole } = within(paginationList);
    const paginationButtons = getAllByRole('listitem');
    expect(paginationList).toBeInTheDocument();
    expect(paginationButtons.length).not.toEqual(0);
  });
  it('renders the prev button', () => {
    render(<YBPagination numPages={20} />);

    expect(screen.getByRole('listitem', { name: /prev/i })).toBeInTheDocument();
  });
  it('renders the next button', () => {
    render(<YBPagination numPages={20} />);

    expect(screen.getByRole('listitem', { name: /next/i })).toBeInTheDocument();
  });
  it('renders the boundary page buttons (numPages=20)', () => {
    render(<YBPagination numPages={20} />);

    expect(screen.getByText('1')).toBeInTheDocument();
    expect(screen.getByText('20')).toBeInTheDocument();
  });
  it('renders the boundary page buttons (numPages=200)', () => {
    render(<YBPagination numPages={200} />);

    expect(screen.getByText('1')).toBeInTheDocument();
    expect(screen.getByText('200')).toBeInTheDocument();
  });
});

// Controlled component test
describe('YBPagination render as controlled component', () => {
  it('renders the pagination button list with positive length', () => {
    render(<YBPagination numPages={20} activePage={10} />);

    const paginationList = screen.getByRole('list');
    const { getAllByRole } = within(paginationList);
    const paginationButtons = getAllByRole('listitem');
    expect(paginationList).toBeInTheDocument();
    expect(paginationButtons.length).not.toEqual(0);
  });
  it('renders the prev button', () => {
    render(<YBPagination numPages={20} activePage={10} />);

    expect(screen.getByRole('listitem', { name: /prev/i })).toBeInTheDocument();
  });
  it('renders the next button', () => {
    render(<YBPagination numPages={20} activePage={10} />);

    expect(screen.getByRole('listitem', { name: /next/i })).toBeInTheDocument();
  });
  it('renders the boundary page buttons (numPages=20)', () => {
    render(<YBPagination numPages={20} activePage={10} />);

    expect(screen.getByText('1')).toBeInTheDocument();
    expect(screen.getByText('20')).toBeInTheDocument();
  });
  it('renders the boundary page buttons (numPages=200)', () => {
    render(<YBPagination numPages={200} activePage={10} />);

    expect(screen.getByText('1')).toBeInTheDocument();
    expect(screen.getByText('200')).toBeInTheDocument();
  });
  it('renders the correct active page given from props (activePage=10)', () => {
    render(<YBPagination numPages={20} activePage={10} />);

    const curPage = screen.getByText('10');
    const { getByText } = within(curPage);
    expect(getByText(/\(current\)/i)).toBeInTheDocument();
  });
  it('rerenders with correct active page given change from props (activePage=8 => activePage=9)', () => {
    const { rerender } = render(<YBPagination numPages={20} activePage={8} />);

    let curPage = screen.getByText('8');
    let scoped = within(curPage);
    expect(scoped.getByText(/\(current\)/i)).toBeInTheDocument();
    rerender(<YBPagination numPages={20} activePage={9} />);
    curPage = screen.getByText('9');
    scoped = within(curPage);
    expect(scoped.getByText(/\(current\)/i)).toBeInTheDocument();
  });
  it('triggers onChange callback function with new page num (valid prev page press)', () => {
    const initialPage = 8;
    const onChange = jest.fn();
    render(<YBPagination numPages={20} activePage={initialPage} onChange={onChange} />);
    userEvent.click(screen.getByRole('listitem', { name: /prev/i }));
    expect(onChange.mock.calls[0][0]).toBe(initialPage - 1);
  });
  it('triggers onChange callback function with new page num (valid next page press)', () => {
    const initialPage = 8;
    const onChange = jest.fn();
    render(<YBPagination numPages={20} activePage={initialPage} onChange={onChange} />);
    userEvent.click(screen.getByRole('listitem', { name: /next/i }));
    expect(onChange.mock.calls[0][0]).toBe(initialPage + 1);
  });
  it('triggers onChange callback function with new page num (valid next page press)', () => {
    const initialPage = 8;
    const onChange = jest.fn();
    render(<YBPagination numPages={20} activePage={initialPage} onChange={onChange} />);
    userEvent.click(screen.getByRole('listitem', { name: /next/i }));
    expect(onChange.mock.calls[0][0]).toBe(initialPage + 1);
  });
  it('triggers onChange callback function with new page num (valid lower boundary page press)', () => {
    const initialPage = 8;
    const onChange = jest.fn();
    render(<YBPagination numPages={20} activePage={initialPage} onChange={onChange} />);
    userEvent.click(screen.getByText('1'));
    expect(onChange.mock.calls[0][0]).toBe(1);
  });
  it('triggers onChange callback function with new page num (valid upper boundary page press)', () => {
    const initialPage = 8;
    const numPages = 20;
    const onChange = jest.fn();
    render(<YBPagination numPages={numPages} activePage={initialPage} onChange={onChange} />);
    userEvent.click(screen.getByText(numPages.toString()));
    expect(onChange.mock.calls[0][0]).toBe(numPages);
  });
});
