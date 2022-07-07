import json
import subprocess
import typing


class CompEntry(typing.NamedTuple):
    directory: str
    command: str
    file: str
    output: str


def parse_compdb():
    json_str = subprocess.check_output(['ninja', '-t', 'compdb'])
    compdb_json = json.loads(json_str)
    result = []
    for entry in compdb_json:
        result.append(CompEntry(
            entry['directory'], entry['command'], entry['file'], entry['output']))
    return result
