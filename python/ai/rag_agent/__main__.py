"""
Main entry point for running the RAG Preprocessor as a package.

This allows users to run:
    python -m rag_agent

OR

    python /path/to/rag_agent
"""

import asyncio
import signal
import logging
from start_rag_agent import main

logger = logging.getLogger(__name__)

# Global flag for graceful shutdown
shutdown_event = None


def handle_signal(signum, frame):
    """Handle Unix signals for graceful shutdown and reloading."""
    global shutdown_event

    signal_name = signal.Signals(signum).name

    if signum == signal.SIGTERM:
        logger.info(f"Received {signal_name} - initiating graceful shutdown...")
        if shutdown_event:
            shutdown_event.set()
    elif signum == signal.SIGINT:
        logger.info(f"Received {signal_name} - initiating graceful shutdown...")
        if shutdown_event:
            shutdown_event.set()
    elif signum == signal.SIGHUP:
        logger.info(f"Received {signal_name} - reloading configuration...")
        # TODO: Implement configuration reload logic
    elif signum == signal.SIGQUIT:
        logger.warning(f"Received {signal_name} - dumping state before shutdown...")
        # TODO: Implement state dump logic (e.g., current tasks, metrics)
    elif signum == signal.SIGUSR1:
        logger.info(f"Received {signal_name} - toggling debug logging...")
        # TODO: Toggle logging level
    elif signum == signal.SIGUSR2:
        logger.info(f"Received {signal_name} - custom action...")
        # TODO: Implement custom action


def setup_signal_handlers():
    """Setup Unix signal handlers for graceful shutdown and management."""
    try:
        # Graceful shutdown signals
        signal.signal(signal.SIGTERM, handle_signal)  # Kill signal from systemd/process manager
        signal.signal(signal.SIGINT, handle_signal)   # Ctrl+C
        signal.signal(signal.SIGHUP, handle_signal)   # Configuration reload
        signal.signal(signal.SIGQUIT, handle_signal)  # Quit with core dump

        # Custom signals for operations
        signal.signal(signal.SIGUSR1, handle_signal)  # Custom action 1
        signal.signal(signal.SIGUSR2, handle_signal)  # Custom action 2

        logger.info("Signal handlers registered successfully")
    except Exception as e:
        logger.error(f"Failed to setup signal handlers: {e}")


if __name__ == "__main__":
    try:
        setup_signal_handlers()
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Process terminated by keyboard interrupt.")
    except Exception as e:
        logger.error(f"Unexpected error: {e}", exc_info=True)
