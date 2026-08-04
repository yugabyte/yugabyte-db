#!/usr/bin/env python

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

"""
This is a command-line tool that allows to get the download URL for a prebuilt third-party
dependencies archive for a particular configuration, as well as to update these URLs based
on the recent releases in the https://github.com/yugabyte/yugabyte-db-thirdparty repository.

Another separate area of functionality of this tool is manipulating "inline 3rd party" dependencies
in the src/inline-thirdparty directory.
"""

import sys
import os
import logging

from typing import DefaultDict, Dict, List, Any, Optional, Pattern, Tuple, Union, Set, cast

from yugabyte.common_util import (
    init_logging,
    make_parent_dir,
)
from yugabyte.file_util import write_file
from yugabyte.thirdparty_tool_impl import (
    get_compilers,
    get_third_party_release,
    parse_args,
    update_thirdparty_dependencies,
)
from yugabyte.thirdparty_archives_metadata import (
    load_manual_metadata,
    load_metadata,
    MetadataItem,
    SHA_KEY,
)

from yugabyte import inline_thirdparty


def main() -> None:
    args = parse_args()
    init_logging(verbose=args.verbose)
    if args.update:
        update_thirdparty_dependencies(args)
        return

    if args.sync_inline_thirdparty:
        inline_thirdparty.sync_inline_thirdparty(args.inline_thirdparty_deps)
        return

    metadata = load_metadata()
    manual_metadata = load_manual_metadata()
    if args.get_sha1:
        print(metadata[SHA_KEY])
        return

    metadata_items = [
        MetadataItem(cast(Dict[str, Any], item_yaml_data))
        for item_yaml_data in (
            cast(List[Dict[str, Any]], metadata['archives']) +
            cast(List[Dict[str, Any]], manual_metadata['archives'])
        )
    ]

    if args.list_compilers:
        compiler_list = get_compilers(
            metadata_items=metadata_items,
            os_type=args.os_type,
            architecture=args.architecture,
            lto=args.lto,
            allow_older_os=args.allow_older_os)
        for compiler in compiler_list:
            print(compiler)
        return

    if args.save_thirdparty_url_to_file or args.save_thirdparty_checksum_url_to_file:
        if not args.compiler_type:
            raise ValueError("Compiler type not specified")

        thirdparty_release = get_third_party_release(
            available_archives=metadata_items,
            compiler_type=args.compiler_type,
            os_type=args.os_type,
            architecture=args.architecture,
            lto=args.lto,
            allow_older_os=args.allow_older_os)

        thirdparty_url = thirdparty_release.url()
        logging.info(f"Download URL for the third-party dependencies: {thirdparty_url}")
        if args.save_thirdparty_url_to_file:
            make_parent_dir(args.save_thirdparty_url_to_file)
            write_file(content=thirdparty_url,
                       output_file_path=args.save_thirdparty_url_to_file)

        thirdparty_checksum_url = thirdparty_release.checksum_url()
        logging.info(f"Checksum URL for the third-party dependencies: {thirdparty_checksum_url}")
        if args.save_thirdparty_checksum_url_to_file:
            make_parent_dir(args.save_thirdparty_checksum_url_to_file)
            write_file(content=thirdparty_checksum_url,
                       output_file_path=args.save_thirdparty_checksum_url_to_file)


if __name__ == '__main__':
    main()
