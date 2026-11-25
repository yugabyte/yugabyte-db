#!/usr/bin/env python3
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
Run hnsw_tool benchmark on a public vector dataset.

Downloads the dataset into the cache directory on first use, then runs hnsw_tool benchmark on its
base vectors, queries and ground truth. Options of this script go before the dataset name. The
arguments after it go to hnsw_tool, and override the dataset's files or distance kind. With any
of them overridden, or --num_vectors_to_insert given, hnsw_tool computes the ground truth unless
--ground_truth_path is given. For a dataset without ground truth, hnsw_tool computes it, and the
script keeps it in the dataset's cache directory for later runs.

Examples:
  bin/vi_benchmark.py siftsmall
  bin/vi_benchmark.py --cache_dir ~/vi_cache sift --ann_method kHnswlib --ef_values 64,128 --k 10
  bin/vi_benchmark.py deep10m --ann_method kHnswlib --k 10,100 --num_validation_queries 1000
"""

import argparse
import glob
import gzip
import math
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile
import zlib
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Set


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CACHE_DIR = os.path.join(REPO_ROOT, 'build', 'vi_cache')
# curl's exit codes when the partial file does not fit the remote file (36), or the server does
# not resume downloads (33).
CURL_RESUME_ERRORS = (33, 36)
# The ground truth of the first Q queries, with the K nearest base vectors of each.
GROUND_TRUTH_CACHE_RE = re.compile(r'gt\.q(\d+)\.k(\d+)\.ivecs')
# hnsw_tool's default --k.
DEFAULT_K = '10'


@dataclass
class Dataset:
    base_file: str
    query_file: str
    num_queries: int
    ground_truth_file: Optional[str] = None
    distance_kind: str = 'kL2Squared'
    # A .tar.gz archive with the files above, or else the URL of their directory.
    archive_url: Optional[str] = None
    files_url: Optional[str] = None

    def files(self) -> List[str]:
        return [name for name in (self.base_file, self.query_file, self.ground_truth_file) if name]


def texmex(name: str, num_queries: int) -> Dataset:
    return Dataset(
        archive_url=f'ftp://ftp.irisa.fr/local/texmex/corpus/{name}.tar.gz',
        base_file=f'{name}_base.fvecs',
        query_file=f'{name}_query.fvecs',
        num_queries=num_queries,
        ground_truth_file=f'{name}_groundtruth.ivecs')


def big_ann(directory: str, num_queries: int, distance_kind: str) -> Dataset:
    return Dataset(
        files_url=f'https://storage.yandexcloud.net/yandex-research/ann-datasets/{directory}/',
        base_file='base.10M.fbin',
        query_file=f'query.public.{num_queries // 1000}K.fbin',
        num_queries=num_queries,
        distance_kind=distance_kind)


DATASETS = {
    # TEXMEX (Inria): siftsmall has 10K 128-dimensional base vectors, sift 1M, and gist 1M
    # 960-dimensional ones.
    'siftsmall': texmex('siftsmall', 100),
    'sift': texmex('sift', 10_000),
    'gist': texmex('gist', 1_000),
    # big-ann-benchmarks (Yandex): 10M 96-dimensional DEEP vectors, and 10M 200-dimensional
    # Text-to-Image ones.
    'deep10m': big_ann('DEEP', 10_000, 'kL2Squared'),
    't2i10m': big_ann('T2I', 100_000, 'kInnerProduct'),
}


def default_tool() -> Optional[str]:
    # Times measured with a debug or sanitizer build are not representative.
    tools = glob.glob(os.path.join(REPO_ROOT, 'build', 'release-*', 'bin', 'hnsw_tool'))
    return max(tools, key=os.path.getmtime) if tools else None


def option_index(option: str, tool_args: List[str]) -> Optional[int]:
    for i, arg in enumerate(tool_args):
        name = arg.split('=', 1)[0]
        # hnsw_tool also takes an unambiguous prefix of an option name.
        if name.startswith('--') and len(name) > 2 and option.startswith(name):
            return i
    return None


def given_options(options: Iterable[str], tool_args: List[str]) -> Set[str]:
    return {option for option in options if option_index(option, tool_args) is not None}


def option_value(option: str, tool_args: List[str]) -> Optional[str]:
    i = option_index(option, tool_args)
    if i is None:
        return None
    _, eq, value = tool_args[i].partition('=')
    return value if eq else next(iter(tool_args[i + 1:]), None)


def cached_ground_truth(dataset: Dataset, dataset_dir: str, tool_args: List[str]) -> List[str]:
    try:
        k_values = (option_value('--k', tool_args) or DEFAULT_K).split(',')
        # hnsw_tool skips empty values.
        max_k = max(int(k) for k in k_values if k.strip())
        num_queries = int(option_value('--num_validation_queries', tool_args) or 0)
    except ValueError:
        # hnsw_tool reports the bad value.
        return []
    # hnsw_tool uses all queries when --num_validation_queries is 0 or exceeds their number.
    if not 0 < num_queries < dataset.num_queries:
        num_queries = dataset.num_queries
    cached = []
    if os.path.isdir(dataset_dir):
        for name in os.listdir(dataset_dir):
            match = GROUND_TRUTH_CACHE_RE.fullmatch(name)
            if match and int(match[1]) >= num_queries and int(match[2]) >= max_k:
                cached.append((int(match[2]), int(match[1]), name))
    if cached:
        return ['--ground_truth_path', os.path.join(dataset_dir, min(cached)[2])]
    # hnsw_tool computes the ground truth and saves it, under a temporary name until it is complete.
    return ['--save_ground_truth_path',
            os.path.join(dataset_dir, f'gt.q{num_queries}.k{max_k}.ivecs')]


def check_fbin_size(path: str, url: str) -> None:
    # An .fbin file starts with the number of vectors and their dimensions as uint32, followed by
    # the float32 coordinates.
    with open(path, 'rb') as f:
        header = f.read(8)
    size = os.path.getsize(path)
    if len(header) < 8 or size != 8 + 4 * math.prod(struct.unpack('<II', header)):
        os.remove(path)
        sys.exit(f'The size of {url}, {size} bytes, does not fit its header; deleted it, '
                 'rerun to download it again')


def download(url: str, path: str) -> None:
    print(f'Downloading {url}', flush=True)
    partial = path + '.part'
    # --continue-at - resumes a download that an earlier run left in the .part file.
    cmd = ['curl', '--fail', '--location', '--show-error', '--continue-at', '-', '--output',
           partial, url]
    returncode = subprocess.call(cmd)
    if returncode in CURL_RESUME_ERRORS:
        os.remove(partial)
        returncode = subprocess.call(cmd)
    if returncode != 0:
        sys.exit(f'Failed to download {url}: curl exit code {returncode}')
    # Over HTTP, curl takes a .part file longer than the remote one for a complete download. A
    # resumed download of a file that changed can also end at another size.
    if path.endswith('.fbin'):
        check_fbin_size(partial, url)
    os.rename(partial, path)


def extract(archive: str, dataset_dir: str, names: List[str]) -> None:
    print(f'Extracting {archive}', flush=True)
    # Renamed once the whole archive is checked, so an existing file is never partial or corrupt.
    parts: Dict[str, str] = {}
    try:
        with gzip.open(archive) as stream:
            # Writes only the named files, so the paths in the archive don't matter.
            with tarfile.open(fileobj=stream, mode='r|') as tar:
                for member in tar:
                    name = os.path.basename(member.name)
                    if member.isfile() and name in names and name not in parts:
                        parts[name] = os.path.join(dataset_dir, name + '.part')
                        with open(parts[name], 'wb') as output:
                            shutil.copyfileobj(tar.extractfile(member), output)
            # gzip checks the CRC of the data at its end. A corrupt archive need not fail before
            # that: tarfile takes a garbled header for the end of the archive.
            while stream.read(1 << 20):
                pass
    except (tarfile.TarError, EOFError, zlib.error, gzip.BadGzipFile) as e:
        for part in parts.values():
            os.remove(part)
        os.remove(archive)
        sys.exit(f'Failed to read {archive}, deleted it, rerun to download it again: {e}')
    missing = set(names) - parts.keys()
    if missing:
        for part in parts.values():
            os.remove(part)
        # The archive passed the CRC check, so downloading it again would not help.
        sys.exit(f'{archive} has no {", ".join(sorted(missing))}; kept the archive')
    for name, part in parts.items():
        os.replace(part, os.path.join(dataset_dir, name))


def prepare(dataset: Dataset, dataset_dir: str, needed: List[str]) -> None:
    if all(os.path.exists(os.path.join(dataset_dir, name)) for name in needed):
        return
    os.makedirs(dataset_dir, exist_ok=True)
    if dataset.archive_url is None:
        for name in needed:
            path = os.path.join(dataset_dir, name)
            if not os.path.exists(path):
                download(dataset.files_url + name, path)
        return
    # All missing files are extracted, so a run with other arguments does not download again.
    missing = [name for name in dataset.files()
               if not os.path.exists(os.path.join(dataset_dir, name))]
    archive = os.path.join(dataset_dir, os.path.basename(dataset.archive_url))
    if not os.path.exists(archive):
        download(dataset.archive_url, archive)
    extract(archive, dataset_dir, missing)
    os.remove(archive)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        # Only the full names of the options below are rejected after the dataset name.
        allow_abbrev=False)
    parser.add_argument('--cache_dir', default=DEFAULT_CACHE_DIR,
                        help='Directory the datasets are downloaded to (default: %(default)s).')
    parser.add_argument('--tool', help='hnsw_tool binary (default: the newest release build).')
    parser.add_argument('--dry_run', action='store_true',
                        help='Print the hnsw_tool command without downloading or running anything.')
    parser.add_argument('dataset', help=f'One of {", ".join(sorted(DATASETS))}.')
    parser.add_argument('tool_args', nargs=argparse.REMAINDER,
                        help='Arguments for hnsw_tool benchmark.')
    # Unknown options before the dataset name are returned, not taken as hnsw_tool arguments.
    args, misplaced = parser.parse_known_args()
    if misplaced:
        parser.error(f'{misplaced[0]}: hnsw_tool arguments go after the dataset name')
    # hnsw_tool has no options with these names.
    for name in [arg.split('=', 1)[0] for arg in args.tool_args]:
        if name in ('--cache_dir', '--tool', '--dry_run'):
            parser.error(f'{name}: options of this script go before the dataset name')
    dataset = DATASETS.get(args.dataset)
    if dataset is None:
        parser.error(f'unknown dataset {args.dataset}; use one of {", ".join(sorted(DATASETS))}')
    tool = args.tool or default_tool()
    # A dry run needs no build.
    if args.dry_run:
        tool = tool or 'hnsw_tool'
    elif tool is None:
        sys.exit('No release build of hnsw_tool; build it with '
                 '"./yb_build.sh release --target hnsw_tool", or pass --tool')
    elif shutil.which(tool) is None:
        sys.exit(f'{tool}: no such executable')

    dataset_dir = os.path.join(args.cache_dir, args.dataset)
    dataset_files = {
        '--build_vecs_path': dataset.base_file,
        '--query_vecs_path': dataset.query_file,
    }
    if dataset.ground_truth_file:
        dataset_files['--ground_truth_path'] = dataset.ground_truth_file
    dataset_args = {option: os.path.join(dataset_dir, name)
                    for option, name in dataset_files.items()}
    dataset_args['--distance_kind'] = dataset.distance_kind
    overridden = given_options([*dataset_args, '--ground_truth_path'], args.tool_args)
    if overridden or given_options(['--num_vectors_to_insert'], args.tool_args):
        # The dataset's ground truth does not fit other or fewer base vectors, other queries or
        # another distance kind, so hnsw_tool computes it, unless --ground_truth_path is given too.
        overridden.add('--ground_truth_path')
    cmd = [tool, 'benchmark']
    for option, value in dataset_args.items():
        # hnsw_tool rejects an option given twice.
        if option not in overridden:
            cmd += [option, value]
    if (dataset.ground_truth_file is None and '--ground_truth_path' not in overridden and
            not given_options(['--save_ground_truth_path'], args.tool_args)):
        cmd += cached_ground_truth(dataset, dataset_dir, args.tool_args)
    cmd += args.tool_args
    print('+ ' + shlex.join(cmd), flush=True)
    if args.dry_run:
        return 0
    needed = [name for option, name in dataset_files.items() if option not in overridden]
    if needed:
        prepare(dataset, dataset_dir, needed)
    return subprocess.call(cmd)


if __name__ == '__main__':
    sys.exit(main())
