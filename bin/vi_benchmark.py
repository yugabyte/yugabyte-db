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
script keeps it in the dataset's cache directory for later runs. --subset runs on the first base
vectors of a dataset, and the script keeps the ground truth of each subset size the same way. For a
subset of an .fbin base file that is not in the cache, only those vectors are downloaded. The output
of hnsw_tool is also written to a log file in --run_log_dir, whose header gives the commands, the
checkout revision and the modification time of hnsw_tool.

Examples:
  bin/vi_benchmark.py --list_datasets
  bin/vi_benchmark.py siftsmall
  bin/vi_benchmark.py --cache_dir ~/vi_cache sift --ann_method kHnswlib --ef_values 64,128 --k 10
  bin/vi_benchmark.py deep10m --ann_method kHnswlib --k 10,100 --num_validation_queries 1000
  bin/vi_benchmark.py --subset 100m deep1b --ann_method kHnswlib --num_validation_queries 1000
"""

import argparse
import datetime
import glob
import gzip
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
from typing import BinaryIO, Dict, Iterable, List, NoReturn, Optional, Set, Tuple


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CACHE_DIR = os.path.join(REPO_ROOT, 'build', 'vi_cache')
# curl's exit codes when the partial file does not fit the remote file (36), or the server does
# not resume downloads (33).
CURL_RESUME_ERRORS = (33, 36)
# The ground truth of the first Q queries, with the K nearest of the first N base vectors (all
# without .nN) for each.
GROUND_TRUTH_CACHE_RE = re.compile(r'gt(?:\.n(\d+))?\.q(\d+)\.k(\d+)\.ivecs')
# The bytes per item of the files that bin_size describes.
BIN_ITEM_SIZES = {'.fbin': 4, '.ibin': 8}
SUBSET_RE = re.compile(r'(\d+)([kmb]?)', re.IGNORECASE)
# hnsw_tool's default --k.
DEFAULT_K = '10'


@dataclass
class Dataset:
    base_file: str
    query_file: str
    num_vectors: int
    dimensions: int
    num_queries: int
    source: str
    ground_truth_file: Optional[str] = None
    distance_kind: str = 'kL2Squared'
    # A .tar.gz archive with the files above, or else the URL of their directory.
    archive_url: Optional[str] = None
    files_url: Optional[str] = None
    # Where the ground truth file is when it is not with the other files.
    ground_truth_url: Optional[str] = None

    def files(self) -> List[str]:
        return [name for name in (self.base_file, self.query_file, self.ground_truth_file) if name]


def texmex(name: str, num_vectors: int, dimensions: int, num_queries: int) -> Dataset:
    return Dataset(
        archive_url=f'ftp://ftp.irisa.fr/local/texmex/corpus/{name}.tar.gz',
        base_file=f'{name}_base.fvecs',
        query_file=f'{name}_query.fvecs',
        num_vectors=num_vectors,
        dimensions=dimensions,
        num_queries=num_queries,
        source='TEXMEX (Inria)',
        ground_truth_file=f'{name}_groundtruth.ivecs')


def parse_subset(value: str) -> int:
    match = SUBSET_RE.fullmatch(value)
    if not match or int(match[1]) == 0:
        raise argparse.ArgumentTypeError(f'expected a positive number with k, m or b, got {value}')
    return int(match[1]) * {'': 1, 'k': 10**3, 'm': 10**6, 'b': 10**9}[match[2].lower()]


def big_ann(directory: str, size: str, dimensions: int, num_queries: int,
            distance_kind: str) -> Dataset:
    url = 'https://storage.yandexcloud.net/yandex-research/ann-datasets/'
    queries = f'public.{num_queries // 1000}K'
    # Only the 1B datasets come with ground truth. The groundtruth.*.ibin files next to their base
    # files have no distances, which hnsw_tool needs; big-ann-benchmarks' own files have them.
    ground_truth = f'{directory.lower()}_new_groundtruth.{queries}' if size == '1B' else None
    return Dataset(
        files_url=f'{url}{directory}/',
        base_file=f'base.{size}.fbin',
        query_file=f'query.{queries}.fbin',
        num_vectors=parse_subset(size),
        dimensions=dimensions,
        num_queries=num_queries,
        source=f'big-ann-benchmarks {directory} (Yandex)',
        # hnsw_tool reads ground truth with distances from .ibin files.
        ground_truth_file=f'{ground_truth}.ibin' if ground_truth else None,
        ground_truth_url=f'{url}{ground_truth}.bin' if ground_truth else None,
        distance_kind=distance_kind)


DATASETS = {
    'siftsmall': texmex('siftsmall', 10_000, 128, 100),
    'sift': texmex('sift', 1_000_000, 128, 10_000),
    'gist': texmex('gist', 1_000_000, 960, 1_000),
    'deep10m': big_ann('DEEP', '10M', 96, 10_000, 'kL2Squared'),
    't2i10m': big_ann('T2I', '10M', 200, 100_000, 'kInnerProduct'),
    'deep1b': big_ann('DEEP', '1B', 96, 10_000, 'kL2Squared'),
    't2i1b': big_ann('T2I', '1B', 200, 100_000, 'kInnerProduct'),
}


def bin_size(rows: int, columns: int, item_size: int) -> int:
    # After an 8-byte header with the numbers of rows and columns as uint32, .fbin files hold n
    # vectors of d float32 coordinates, and .ibin ground truth files the ids and distances of k
    # neighbors of n queries.
    return 8 + item_size * rows * columns


def fbin_size(num_vectors: int, dimensions: int) -> int:
    return bin_size(num_vectors, dimensions, BIN_ITEM_SIZES['.fbin'])


def base_file_size(dataset: Dataset) -> int:
    if dataset.base_file.endswith('.fvecs'):
        # .fvecs prefixes each vector with its dimensions as int32.
        return dataset.num_vectors * 4 * (1 + dataset.dimensions)
    if dataset.base_file.endswith('.fbin'):
        return fbin_size(dataset.num_vectors, dataset.dimensions)
    raise ValueError(f'Unknown format of {dataset.base_file}')


def format_size(size: int) -> str:
    for unit, scale in (('GB', 1e9), ('MB', 1e6)):
        if size >= scale:
            return f'{size / scale:.1f} {unit}'
    return f'{size / 1e3:.1f} KB'


def list_datasets() -> None:
    rows = [('dataset', 'vectors', 'dimensions', 'queries', 'distance', 'base file', 'source')]
    for name, dataset in DATASETS.items():
        rows.append((name, f'{dataset.num_vectors:,}', str(dataset.dimensions),
                     f'{dataset.num_queries:,}', dataset.distance_kind,
                     format_size(base_file_size(dataset)), dataset.source))
    widths = [max(len(row[i]) for row in rows) for i in range(len(rows[0]))]
    numeric = (1, 2, 3, 5)
    for row in rows:
        print('  '.join(cell.rjust(width) if i in numeric else cell.ljust(width)
                        for i, (cell, width) in enumerate(zip(row, widths))).rstrip())


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


def cached_ground_truth(dataset: Dataset, dataset_dir: str, subset: Optional[int],
                        tool_args: List[str]) -> List[str]:
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
            if (match and (match[1] and int(match[1])) == subset and
                    int(match[2]) >= num_queries and int(match[3]) >= max_k):
                cached.append((int(match[3]), int(match[2]), name))
    if cached:
        return ['--ground_truth_path', os.path.join(dataset_dir, min(cached)[2])]
    # hnsw_tool computes the ground truth and saves it, under a temporary name until it is complete.
    subset_tag = f'.n{subset}' if subset else ''
    return ['--save_ground_truth_path',
            os.path.join(dataset_dir, f'gt{subset_tag}.q{num_queries}.k{max_k}.ivecs')]


def check_bin_size(path: str, url: str, item_size: int) -> None:
    with open(path, 'rb') as f:
        header = f.read(8)
    size = os.path.getsize(path)
    if len(header) < 8 or size != bin_size(*struct.unpack('<II', header), item_size):
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
    item_size = BIN_ITEM_SIZES.get(os.path.splitext(path)[1])
    if item_size:
        check_bin_size(partial, url, item_size)
    os.rename(partial, path)


def curl_version() -> Tuple[int, int]:
    output = subprocess.run(['curl', '--version'], capture_output=True, text=True).stdout
    match = re.match(r'curl (\d+)\.(\d+)', output)
    return (int(match[1]), int(match[2])) if match else (0, 0)


def download_prefix(url: str, path: str, num_vectors: int, dimensions: int) -> None:
    print(f'Downloading the first {num_vectors:,} vectors of {url}', flush=True)
    size = fbin_size(num_vectors, dimensions)
    partial = path + '.part'
    start = os.path.getsize(partial) if os.path.exists(partial) else 0
    if start < size:
        # --max-filesize stops a server that ignores --range and sends the whole file. Without a
        # Content-Length, only curl 8.4 and newer stop it.
        version = curl_version()
        if version < (8, 4):
            sys.exit(f'Downloading part of {url} needs curl 8.4 or newer, found '
                     f'{version[0]}.{version[1]}')
        # curl drops the end of --range when it resumes with --continue-at, so the rest of the
        # range is appended here.
        with open(partial, 'ab') as output:
            returncode = subprocess.call(
                ['curl', '--fail', '--location', '--show-error', '--range', f'{start}-{size - 1}',
                 '--max-filesize', str(size - start), '--output', '-', url], stdout=output)
        if returncode == 63:
            # Without a Content-Length, curl stops only after writing the start of the file.
            os.truncate(partial, start)
            sys.exit(f'{url} does not support HTTP Range requests; download the whole file')
        if returncode != 0:
            sys.exit(f'Failed to download {url}: curl exit code {returncode}')
    with open(partial, 'r+b') as f:
        file_dimensions = None
        if os.path.getsize(partial) == size:
            _, file_dimensions = struct.unpack('<II', f.read(8))
        if file_dimensions != dimensions:
            f.close()
            os.remove(partial)
            sys.exit(f'The first {size} bytes of {url} do not hold {num_vectors} vectors of '
                     f'{dimensions} dimensions; deleted them, rerun to download them again')
        # The header gives the number of vectors in the whole file.
        f.seek(0)
        f.write(struct.pack('<I', num_vectors))
    os.rename(partial, path)


def subset_base_file(dataset: Dataset, dataset_dir: str, subset: int) -> Tuple[str, bool]:
    """Returns the base file for the first subset vectors, and whether it has more of them."""
    if os.path.exists(os.path.join(dataset_dir, dataset.base_file)):
        return dataset.base_file, True
    stem, extension = os.path.splitext(dataset.base_file)
    if extension != '.fbin' or dataset.files_url is None:
        return dataset.base_file, True
    # The smallest downloaded prefix with enough vectors, or else a new one.
    prefix_re = re.compile(re.escape(stem) + r'\.n(\d+)\.fbin')
    prefixes = []
    if os.path.isdir(dataset_dir):
        for name in os.listdir(dataset_dir):
            match = prefix_re.fullmatch(name)
            if match and int(match[1]) >= subset:
                prefixes.append((int(match[1]), name))
    if prefixes:
        num_vectors, name = min(prefixes)
        return name, num_vectors > subset
    return f'{stem}.n{subset}.fbin', False


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
                url = dataset.files_url + name
                if name == dataset.ground_truth_file and dataset.ground_truth_url:
                    url = dataset.ground_truth_url
                download(url, path)
        return
    # All missing files are extracted, so a run with other arguments does not download again.
    missing = [name for name in dataset.files()
               if not os.path.exists(os.path.join(dataset_dir, name))]
    archive = os.path.join(dataset_dir, os.path.basename(dataset.archive_url))
    if not os.path.exists(archive):
        download(dataset.archive_url, archive)
    extract(archive, dataset_dir, missing)
    os.remove(archive)


def git_revision() -> str:
    try:
        return subprocess.run(
            ['git', '-C', REPO_ROOT, 'describe', '--always', '--dirty', '--exclude=*'],
            capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'


def cannot_write_log(error: str) -> NoReturn:
    sys.exit(f'Cannot write the run log: {error}; pass --run_log_dir or --no_run_log')


def make_log_dir(log_dir: str) -> None:
    try:
        os.makedirs(log_dir, exist_ok=True)
    except OSError as e:
        cannot_write_log(str(e))
    if not os.access(log_dir, os.W_OK):
        cannot_write_log(f'{log_dir} is not writable')


def open_log(log_dir: str, dataset_name: str) -> BinaryIO:
    name = f'{datetime.datetime.now():%Y%m%d-%H%M%S}.{dataset_name}'
    suffix = ''
    while True:
        try:
            return open(os.path.join(log_dir, f'{name}{suffix}.log'), 'xb')
        except FileExistsError:
            # Another run started in the same second.
            suffix = f'.{int(suffix[1:] or 1) + 1}'
        except OSError as e:
            cannot_write_log(str(e))


def modification_time(path: str) -> str:
    return datetime.datetime.fromtimestamp(os.path.getmtime(path)).isoformat(timespec='seconds')


def run_logged(cmd: List[str], log: BinaryIO) -> int:
    tool_path = shutil.which(cmd[0]) or cmd[0]
    header = [
        f'time: {datetime.datetime.now().isoformat(timespec="seconds")}',
        # hnsw_tool may be older than the checkout, or come from another one.
        f'checkout revision: {git_revision()}',
        f'hnsw_tool modified: {modification_time(tool_path)}',
        f'script command: {shlex.join(sys.argv)}',
        f'hnsw_tool command: {shlex.join(cmd)}',
    ]
    log.write(''.join(f'# {line}\n' for line in header).encode())
    log.flush()
    # stderr goes to the pipe too, so the log keeps the order of the lines.
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert proc.stdout is not None
    with proc:
        try:
            for line in proc.stdout:
                sys.stdout.buffer.write(line)
                sys.stdout.buffer.flush()
                log.write(line)
                log.flush()
        except BaseException:
            # Otherwise the benchmark keeps running unattended until it ends.
            proc.kill()
            raise
    return proc.returncode


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
    parser.add_argument('--list_datasets', action='store_true',
                        help='Print the datasets with their sizes and distance kinds.')
    parser.add_argument('--run_log_dir',
                        help='Directory for the run logs (default: <cache_dir>/logs).')
    parser.add_argument('--no_run_log', action='store_true', help='Do not write a run log.')
    parser.add_argument('--subset', type=parse_subset,
                        help='Number of base vectors to run on, from the start of the dataset, '
                             'with an optional k, m or b suffix.')
    parser.add_argument('dataset', nargs='?', help=f'One of {", ".join(sorted(DATASETS))}.')
    parser.add_argument('tool_args', nargs=argparse.REMAINDER,
                        help='Arguments for hnsw_tool benchmark.')
    # Unknown options before the dataset name are returned, not taken as hnsw_tool arguments.
    args, misplaced = parser.parse_known_args()
    if misplaced:
        parser.error(f'{misplaced[0]}: hnsw_tool arguments go after the dataset name')
    # hnsw_tool has no options with these names.
    for name in [arg.split('=', 1)[0] for arg in args.tool_args]:
        if name in ('--cache_dir', '--tool', '--dry_run', '--list_datasets',
                    '--run_log_dir', '--no_run_log', '--subset'):
            parser.error(f'{name}: options of this script go before the dataset name')
    if args.list_datasets:
        list_datasets()
        return 0
    if args.dataset is None:
        parser.error('the dataset name is required')
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

    subset = args.subset
    if subset is not None:
        for option in sorted(given_options(['--build_vecs_path', '--num_vectors_to_insert'],
                                           args.tool_args)):
            parser.error(f'{option} does not go with --subset')
        if subset > dataset.num_vectors:
            parser.error(f'--subset {subset} exceeds the {dataset.num_vectors} vectors of '
                         f'{args.dataset}')
        if subset == dataset.num_vectors:
            subset = None

    dataset_dir = os.path.join(args.cache_dir, args.dataset)
    base_file, insert_subset = dataset.base_file, False
    if subset:
        base_file, insert_subset = subset_base_file(dataset, dataset_dir, subset)
    dataset_files = {
        '--build_vecs_path': base_file,
        '--query_vecs_path': dataset.query_file,
    }
    # The dataset's ground truth is for all its base vectors.
    if dataset.ground_truth_file and not subset:
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
    if insert_subset:
        cmd += ['--num_vectors_to_insert', str(subset)]
    if ('--ground_truth_path' not in dataset_files and '--ground_truth_path' not in overridden and
            not given_options(['--save_ground_truth_path'], args.tool_args)):
        cmd += cached_ground_truth(dataset, dataset_dir, subset, args.tool_args)
    cmd += args.tool_args
    print('+ ' + shlex.join(cmd), flush=True)
    if args.dry_run:
        return 0
    log_dir = None
    if not args.no_run_log:
        log_dir = args.run_log_dir or os.path.join(args.cache_dir, 'logs')
        # Checked before the download, which can take long.
        make_log_dir(log_dir)
    needed = [name for option, name in dataset_files.items() if option not in overridden]
    if base_file != dataset.base_file:
        needed.remove(base_file)
        path = os.path.join(dataset_dir, base_file)
        if not os.path.exists(path):
            os.makedirs(dataset_dir, exist_ok=True)
            download_prefix(dataset.files_url + dataset.base_file, path, subset,
                            dataset.dimensions)
    if needed:
        prepare(dataset, dataset_dir, needed)
    if log_dir is None:
        return subprocess.call(cmd)
    with open_log(log_dir, args.dataset) as log:
        try:
            return run_logged(cmd, log)
        finally:
            print(f'Log: {log.name}', flush=True)


if __name__ == '__main__':
    sys.exit(main())
