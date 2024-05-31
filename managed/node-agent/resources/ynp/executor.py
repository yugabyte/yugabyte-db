from commands.provision_command import ProvisionCommand

class Executor:
    def __init__(self, config, args):
        self.config = config
        self.args = args
        self.commands = {
            "provision": ProvisionCommand
        }

    def exec(self):
        command_class = self.commands.get(self.args.command)
        if not command_class:
            raise ValueError(f"Unsupported command: {self.args.command}")
        command_instance = command_class(self.config)
        command_instance.execute()
