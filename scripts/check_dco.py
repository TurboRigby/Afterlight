# SPDX-License-Identifier: MPL-2.0
"""Require an author-matching DCO trailer on each commit in a revision range."""
import argparse
from pathlib import Path
import re
import subprocess
import sys


def git(repo, *args, input=None):
    return subprocess.run(['git', '-C', str(repo), *args], input=input,
                          text=True, encoding='utf-8', errors='replace',
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=True).stdout


def check(repo, base, head):
    # Resolve first: revision names cannot become options or arbitrary ranges.
    base = git(repo, 'rev-parse', '--verify', '--end-of-options', base + '^{commit}').strip()
    head = git(repo, 'rev-parse', '--verify', '--end-of-options', head + '^{commit}').strip()
    commits = git(repo, 'rev-list', '--reverse', base + '..' + head).splitlines()
    failures = []
    for commit in commits:
        name, email, body = git(repo, 'show', '-s', '--format=%an%x00%ae%x00%B', commit).split('\0', 2)
        trailers = git(repo, 'interpret-trailers', '--parse', input=body)
        signers = re.findall(r'^Signed-off-by:\s+(.+?)\s+<([^<>\s]+)>\s*$',
                             trailers, flags=re.IGNORECASE | re.MULTILINE)
        if (name, email) not in signers:
            failures.append((commit, name, email))
    return len(commits), failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--base', required=True)
    parser.add_argument('--head', default='HEAD')
    args = parser.parse_args()
    try:
        count, failures = check(args.repo, args.base, args.head)
    except subprocess.CalledProcessError as error:
        print('DCO check could not read the commit range: ' + error.stderr.strip(), file=sys.stderr)
        return 2
    for commit, name, email in failures:
        print(f'{commit[:12]}: missing author sign-off: Signed-off-by: {name} <{email}>', file=sys.stderr)
    if failures:
        print('Read CONTRIBUTING.md and DCO, then sign off commits you can certify with git commit --signoff.', file=sys.stderr)
        return 1
    print(f'DCO: all {count} introduced commits have author-matching sign-offs.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
