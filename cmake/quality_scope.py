"""Lists the project files one quality check must look at when only changed files are checked.

--kind lizard: changed C++ files under src/.
--kind tidy:   changed .cpp files under src/, plus every .cpp under src/ that directly includes a changed
               header (includes are written relative to src/, e.g. "core/format/ByteSize.h").
"Changed" means different from --base (default HEAD) in the working tree, or untracked.
Paths are written one per line, relative to the repository root, with forward slashes.
"""
import argparse
import pathlib
import subprocess

SOURCE_SUFFIXES = {'.cpp', '.cc', '.cxx'}
HEADER_SUFFIXES = {'.h', '.hpp', '.hh', '.hxx'}


def git_lines(root, *args):
    """Runs git in root and returns its non-empty output lines."""
    result = subprocess.run(['git', '-C', str(root), *args], check=True, capture_output=True, text=True)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def changed_files(root, base):
    """Changed or untracked files under src/ that still exist, relative to root."""
    names = git_lines(root, 'diff', '--name-only', base, '--') + git_lines(
        root, 'ls-files', '--others', '--exclude-standard')
    return sorted({n for n in names if n.startswith('src/') and (root / n).is_file()})


def includers(root, headers):
    """Translation units under src/ that directly include one of the given headers."""
    spellings = {f'"{h[len("src/"):]}"' for h in headers}
    units = []
    for path in sorted((root / 'src').rglob('*')):
        if path.suffix in SOURCE_SUFFIXES:
            text = path.read_text(encoding='utf-8', errors='replace')
            if any(spelling in text for spelling in spellings):
                units.append(path.relative_to(root).as_posix())
    return units


def main():
    """Parses arguments, selects the files and writes the list."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--kind', choices=['lizard', 'tidy'], required=True)
    parser.add_argument('--root', required=True)
    parser.add_argument('--base', default='HEAD')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()
    changed = changed_files(root, args.base)
    cpp = [n for n in changed if pathlib.PurePosixPath(n).suffix in SOURCE_SUFFIXES]
    headers = [n for n in changed if pathlib.PurePosixPath(n).suffix in HEADER_SUFFIXES]
    if args.kind == 'lizard':
        selected = cpp + headers
    else:
        selected = sorted(set(cpp) | set(includers(root, headers)))
    pathlib.Path(args.output).write_text(''.join(f'{n}\n' for n in selected), encoding='utf-8')


if __name__ == '__main__':
    main()
