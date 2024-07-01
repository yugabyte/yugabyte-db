import logging

logger = logging.getLogger(__name__)

class Command:
    def __init__(self, config):
        self.config = config

    def execute(self):
        raise NotImplementedError("Each command must implement the execute method.")

    def validate(self):
        # perform basic validation code
        pass
