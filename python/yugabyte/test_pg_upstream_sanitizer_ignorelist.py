# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

import pathlib

import pytest

from yugabyte import pg_upstream_sanitizer_ignorelist as ignorelist


SOURCE = '''\
#include "postgres.h"

static int    helper(int x);

/*
 * A comment.
 */
static int
helper(int x)
{
    return x + 1;
}

Datum
entry_point(PG_FUNCTION_ARGS)
{
    int            y = helper(1);

    PG_RETURN_INT32(y);
}
'''.replace('    ', '\t')


def test_parse_functions() -> None:
    functions = ignorelist.parse_functions(SOURCE)
    assert sorted(functions) == ['entry_point', 'helper']
    assert functions['helper'] == 'static int\nhelper(int x)\n{\n\treturn x + 1;\n}'


def test_parse_functions_skips_duplicates() -> None:
    source = SOURCE + '\nstatic int\nhelper(int x)\n{\n\treturn x;\n}\n'
    assert sorted(ignorelist.parse_functions(source)) == ['entry_point']


@pytest.fixture
def repo(tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> pathlib.Path:
    (tmp_path / 'src').mkdir()
    (tmp_path / 'src' / 'listed_file.c').write_text(SOURCE)
    (tmp_path / 'src' / 'listed_function.c').write_text(SOURCE)
    functions = ignorelist.parse_functions(SOURCE)
    list_path = tmp_path / 'list.txt'
    list_path.write_text(
        f'# file src/listed_file.c {ignorelist.content_hash(SOURCE)}\n'
        f'src:*/src/listed_file.c\n'
        f'# funcs src/listed_function.c helper={ignorelist.content_hash(functions["helper"])}\n'
        f'fun:helper\n')
    monkeypatch.setattr(ignorelist, 'REPO_ROOT', str(tmp_path))
    monkeypatch.setattr(ignorelist, 'LIST_PATH', str(list_path))
    return tmp_path


def test_check_unchanged(repo: pathlib.Path) -> None:
    assert ignorelist.check(['src/listed_file.c', 'src/listed_function.c', 'src/other.c']) == []


def test_check_changed_file(repo: pathlib.Path) -> None:
    (repo / 'src' / 'listed_file.c').write_text(SOURCE + '\n')
    messages = ignorelist.check(['src/listed_file.c'])
    assert len(messages) == 1
    assert messages[0].startswith('error:stale_sanitizer_ignorelist:The file is listed')


def test_check_changed_function(repo: pathlib.Path) -> None:
    # A change to an unlisted function is fine; a change to a listed one is not.
    path = repo / 'src' / 'listed_function.c'
    path.write_text(SOURCE.replace('PG_RETURN_INT32(y);', 'PG_RETURN_INT32(y + 1);'))
    assert ignorelist.check(['src/listed_function.c']) == []
    path.write_text(SOURCE.replace('return x + 1;', 'return x + 2;'))
    messages = ignorelist.check(['src/listed_function.c'])
    assert len(messages) == 1
    assert 'Function helper is listed' in messages[0]
