import { Component, ErrorInfo } from 'react';

type ErrorBoundaryState = {
  hasError: boolean;
};
export class ErrorBoundary extends Component {
  public state: ErrorBoundaryState = {
    hasError: false
  };

  static getDerivedStateFromError(error: Error) {
    return { hasError: true };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    //if error == '401, , then log and display permission needed
    console.error(error, info);
  }

  render() {
    if (this.state.hasError) {
      return <h1>Something went wrong.</h1>;
    }

    return this.props.children;
  }
}
