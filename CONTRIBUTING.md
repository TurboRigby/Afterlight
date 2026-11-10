# Contributing to Afterlight

Afterlight's engine, editor and game code is C17; rendering uses Vulkan and GLSL.
Keep changes focused, preserve existing license notices, and run the checks
relevant to the change. See [building](README.md) and [verification](docs/testing.md).

## Required DCO sign-off

Every new contribution commit must carry the author's `Signed-off-by` trailer.
By adding it, you certify the [Developer Certificate of Origin 1.1](DCO), whose
original text is available at [developercertificate.org](https://developercertificate.org/).
Read it before signing off. This records your right to submit the contribution
under its applicable license; Afterlight's original code remains MPL-2.0 and
third-party files retain their own licenses.

Set Git's author identity to the name and email you contribute under, then use:

```sh
git commit --signoff -m "Describe the change"
# Short form:
git commit -s -m "Describe the change"
```

Git adds a trailer using your configured identity:

```text
Signed-off-by: Your Name <you@example.com>
```

The name and email must match the commit author. A GitHub noreply address is
fine when it is also the author's email. A GPG/SSH commit signature is separate
and does not replace this trailer. Preserve other contributors' authorship and
existing sign-offs; do not add a sign-off on someone else's behalf.

If you forgot your sign-off on your latest commit and can certify the DCO:

```sh
git commit --amend --no-edit --signoff
```

For several commits on your own contribution branch, you can review each commit
with `git rebase -i BASE`, mark unsigned commits `edit`, amend each with the
command above, then run `git rebase --continue`. Rewriting published commits
changes their IDs; coordinate with collaborators before updating that branch.
Existing project history predating this policy does not need retroactive sign-offs.

The **DCO / Sign-off** pull-request check validates every commit introduced by
the PR, including contributor-created merge commits. The GitHub-generated test
merge commit is excluded. Maintainers should require this status check in the
`main` branch ruleset before merging, retain sign-offs when squashing, and use
`git merge --signoff` when creating local merge commits. Changing a workflow file
alone does not enable branch protection or enforce direct pushes.

Run the same check locally, using the base and head revisions for your change:

```sh
python3 scripts/check_dco.py --base origin/main --head HEAD
python3 tests/tooling/dco.py
```
