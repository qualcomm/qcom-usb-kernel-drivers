## Contributing to Qualcomm USB Kernel Drivers

Hi there!

We’re thrilled that you’d like to contribute to this project.
Your help is essential for keeping this project great and for making it better.

This document follows open source best practices and Qualcomm's
[Open Source Developer Office (OSDO) contribution guidelines](https://github.qualcomm.com/pages/osdo/handbook/qcom-github/docs/upstream-github-workflows/).
Please read it fully before submitting your first pull request.

---

## Table of Contents
1. [Code of Conduct & License](#code-of-conduct--license)
2. [Ways to Contribute](#ways-to-contribute)
3. [Development Workflow](#development-workflow)
   - [Branching Strategy](#branching-strategy)
     - [Core principles](#core-principles)
   - [Branch Naming Conventions](#branch-naming-conventions)
   - [Pull Request Process](#pull-request-process)
     - [Before Submitting](#before-submitting)
     - [Sync with Upstream](#sync-with-upstream)
     - [Submitting Your Pull Request](#submitting-your-pull-request)
     - [PR description & checklist](#pr-description--checklist)
   - [Commit Message Guidelines](#commit-message-guidelines)
4. [Workflow Diagrams](#workflow-diagrams)
   - [Standard Feature / Bugfix Flow](#standard-feature--bugfix-flow)
   - [Hotfix Flow (contributor view)](#hotfix-flow-contributor-view)
   - [Submitting Your Pull Request Flow](#submitting-your-pull-request-flow)
5. [Code Style Guidelines](#code-style-guidelines)
   - [Linux Kernel Drivers (C)](#linux-kernel-drivers-c)
   - [Linux User-space Applications (C / C++)](#linux-user-space-applications-c--c)
   - [Windows Kernel Drivers (C)](#windows-kernel-drivers-c)
   - [Windows User-space Applications (C++ / C#)](#windows-user-space-applications-c--c-1)
   - [Python (Build / Tooling Scripts)](#python-build--tooling-scripts)
6. [Testing Requirements](#testing-requirements)
   - [Where to find build & test instructions](#where-to-find-build--test-instructions)
   - [Minimum expectations before you open a PR](#minimum-expectations-before-you-open-a-pr)
   - [When in doubt](#when-in-doubt)
7. [Review Process](#review-process)
   - [Things that increase the chance of acceptance](#things-that-increase-the-chance-of-acceptance)
8. [Maintainer Responsibilities](#maintainer-responsibilities)
9. [FAQ](#faq)
   - [Why do I have to *fork* the repository?](#why-do-i-have-to-fork-the-repository-why-not-just-push-a-branch)
   - [Why typed branch names?](#why-do-we-use-branch-names-like-feature-bugfix-usersme)
   - [Do I need DCO sign-off on every commit?](#do-i-need-dco-sign-off-on-every-commit)
   - [What if my PR has many small "fix typo" commits?](#what-if-my-pr-has-many-small-fix-typo-commits)
   - [My PR has conflicts — should I use GitHub's "Resolve conflicts" button?](#my-pr-has-conflicts--should-i-use-githubs-resolve-conflicts-button)
   - [Where do I report a security issue?](#where-do-i-report-a-security-issue)

---

## Code of Conduct & License

By participating in this project you agree to abide by our
[Code of Conduct](CODE-OF-CONDUCT.md) and accept the terms of the project
[LICENSE](LICENSE.txt). All contributions must be DCO-signed
(see [Submitting a Pull Request](#submitting-a-pull-request)).

---

## Ways to Contribute

- **Report bugs** or **propose features** via [GitHub Issues](https://github.com/qualcomm/qcom-usb-kernel-drivers/issues).
- **Improve documentation** — typos, examples, clarifications are very welcome.
- **Submit code changes** — bug fixes, performance improvements, new features, tests.
- **Review pull requests** from other contributors.

For non-trivial changes, please **open an issue first** so that design and scope
can be discussed before code is written. This avoids wasted effort.

---

## Development Workflow

This section covers everything you need to do to land a change — branching,
naming, opening the pull request, and writing the commit message.

### Branching Strategy

This project follows a standard open-source branching model:

| Branch          | Purpose                                                      |
|-----------------|--------------------------------------------------------------|
| `main`          | Stable, release-ready code. Tagged with semantic versions.   |
| `develop`       | Integration branch where contributor PRs land first.         |
| `release/x.y`   | Created on demand to maintain an older release with hotfixes.|

#### Core principles
- `main` is **always** deployable; every commit on it should be shippable.
- **All changes** enter through pull requests — never direct pushes.
- Contributors work in **forks**; maintainers work in named branches in the upstream repo.
- History stays linear where possible — **prefer rebase over merge commits**.
- Branches are **short-lived** — one logical change per branch, deleted after merge.

### Branch Naming Conventions

Use a consistent naming convention for your branches so contributors and
reviewers can identify the **type of work** and the **owner** at a glance.
The convention combines a *type prefix* with a short, hyphenated
description; you may optionally include the owner's username or the
related work-item ID.

| Prefix                          | Use for                                            | Example                                  |
|---------------------------------|----------------------------------------------------|------------------------------------------|
| `users/<username>/<description>`| Personal working branch (any work-in-progress)     | `users/shasaror/usbnet-retry-experiment` |
| `feature/<feature-name>`        | New feature or capability                          | `feature/add-retry-logic`                |
| `feature/<area>/<feature-name>` | Feature within a specific subsystem / area         | `feature/qcom_usbnet/add-retry-logic`         |
| `bugfix/<description>`          | Bug fix found in normal development                | `bugfix/null-pointer-on-empty-input`     |
| `hotfix/<description>`          | Urgent fix targeted at a `release/x.y` branch      | `hotfix/dma-leak-on-disconnect`          |
| `docs/<description>`            | Documentation only                                 | `docs/getting-started`                   |
| `refactor/<description>`        | Code restructuring, no behavior change             | `refactor/extract-parser-module`         |
| `test/<description>`            | Adding/updating tests                              | `test/add-missing-edge-cases`            |
| `ci/<description>`              | CI / build-pipeline changes                        | `ci/migrate-to-github-actions`           |

This style:
- Identifies the **type of change** (feature / bugfix / hotfix / docs …).
- Identifies the **owner** when you use the `users/<username>/…` form,
  which is helpful in shared forks and during multi-team development.
- Keeps branches greppable: `git branch --list 'feature/usbnet/*'`.

Guidelines:
1. Branch from the **latest `develop`** (or from the relevant
   `release/x.y` for a `hotfix/`).
2. Keep branches **short-lived** — one logical change per branch.
3. **Rebase** onto the target branch before requesting review (avoid merge commits in PRs).
4. **Delete** the branch in your fork after the PR is merged.

### Pull Request Process

#### Before Submitting

These are **prerequisites** for every PR. The actual per-PR sequence
of git commands is in [Submitting Your Pull Request](#submitting-your-pull-request)
below — this section just lists what must already be true (and the
**one-time setup** every contributor does once per machine) before you
start that sequence.

**One-time setup (do this once per machine):**

1. [Fork](https://github.com/qualcomm/qcom-usb-kernel-drivers/fork) `qualcomm/qcom-usb-kernel-drivers` to your personal GitHub
   account (Fork button → your account). [Why fork? see FAQ.](#why-do-i-have-to-fork-the-repository)
2. **Clone** your fork and add the upstream remote:
   ```bash
   git clone https://github.com/<your-username>/qcom-usb-kernel-drivers.git
   cd qcom-usb-kernel-drivers
   git remote add upstream https://github.com/qualcomm/qcom-usb-kernel-drivers.git
   git fetch upstream
   ```

**Per-PR prerequisites (verify before you start the *Submitting* sequence):**

- You have read our [Code of Conduct](CODE-OF-CONDUCT.md) and [LICENSE](LICENSE.txt).
- For non-trivial changes, you have **opened an issue first** to discuss
  design and scope.
- You have **built and tested locally** following the steps in the
  [project README](README.md) / [src/linux/README.md](src/linux/README.md)
  and verified your change does not break existing behavior.
- You will **verify that all required CI checks are green on your fork's
  branch *before* opening the PR upstream.** The same workflows that run on
  `qualcomm/qcom-usb-kernel-drivers` also run on every push to your fork —
  push your branch first, wait for the green tick on the latest commit,
  *then* open the PR. Opening a PR while CI is still red wastes reviewer
  time and will be sent back for changes.
- Each commit will be **signed off** (DCO — see *Submitting Your Pull
  Request* step 3 below).
- Your branch is **focused on a single logical change**.

#### Sync with Upstream

Before pushing for review, rebase your branch onto the latest target
branch so the PR applies cleanly and CI runs against current code.

Command:
```bash
git fetch upstream
git checkout <branch-prefix>/<area>/<description>
git rebase upstream/<target-branch>      # develop, or release/x.y for hotfix/
```
Example (topic branch):
```bash
git fetch upstream
git checkout feature/qcom_usbnet/add-retry-logic
git rebase upstream/develop
```
Example (hotfix branch):
```bash
git fetch upstream
git checkout hotfix/dma-leak-on-disconnect
git rebase upstream/release/1.2
```

> Note: do **not** rebase onto `upstream/main`. Contributor branches
> always target `develop` (or `release/x.y` for `hotfix/`) — `main` only
> receives changes when a release is promoted from `develop`.

#### Submitting Your Pull Request

> Assumes you've completed the **one-time setup** in
> [Before Submitting](#before-submitting) (forked the repo, cloned, and
> added the `upstream` remote). The steps below are what you do **for
> every PR**, in order.

1. **Create a topic branch** from the latest `develop`, using the
   [naming conventions](#branch-naming-conventions).

   Command:
   ```bash
   git checkout -b <branch-prefix>/<area>/<description> upstream/develop
   ```
   Example:
   ```bash
   git checkout -b feature/qcom_usbnet/add-retry-logic upstream/develop
   ```

2. **Make your changes**, add/update tests, and make sure existing tests pass.

3. **Commit with DCO sign-off** following the
   [Commit Message Guidelines](#commit-message-guidelines).
   Every commit must carry a `Signed-off-by:` trailer (`git commit -s`);
   commits without one will be rejected.

4. **Rebase** onto the latest target branch before pushing — avoid merge commits in your PR.

   Command:
   ```bash
   git fetch upstream
   git rebase upstream/<target-branch>      # develop, or release/x.y for hotfix/
   ```
   Example (topic branch):
   ```bash
   git fetch upstream
   git rebase upstream/develop
   ```
   Example (hotfix branch — never rebase onto `upstream/main`):
   ```bash
   git fetch upstream
   git rebase upstream/release/1.2
   ```

5. **Push** to your fork.

   Command:
   ```bash
   git push -u origin <branch-prefix>/<area>/<description>
   ```
   Example:
   ```bash
   git push -u origin feature/qcom_usbnet/add-retry-logic
   ```

6. **Open a Pull Request** from your fork's branch → the upstream
   `develop` branch (or the relevant `release/x.y` for hotfixes).
   - Use a clear, descriptive PR title (see [Commit Message Guidelines](#commit-message-guidelines)).
   - Reference the issue your PR addresses (e.g. `Fixes #123`).
   - Describe **what** changed and **why**.
   - Mark breaking changes explicitly.

7. **Respond to review feedback**. Push additional commits to the same branch;
   the PR will update automatically. Squash/rebase before final merge if requested.

#### PR description & checklist

When you open the PR, GitHub will pre-fill the description from the project's
[Pull Request Template](.github/PULL_REQUEST_TEMPLATE.md).
Please read through it carefully, fill in every section, and tick every
checklist item before marking the PR as ready for review — do not delete
any sections.

### Commit Message Guidelines

Use the [Conventional Commits](https://www.conventionalcommits.org/) format.
Every commit must include a `Signed-off-by:` trailer — by signing off you
certify the [Developer Certificate of Origin](http://developercertificate.org/).

```
<type>/<optional-scope>: <short summary>

<optional body — what and why, not how>

<optional footer — e.g. "Fixes #123", "BREAKING CHANGE: ...">

Signed-off-by: Your Name <your.email@example.com>
```

**Rules:**
- **Subject line**: `<type>/<scope>: <short summary>`, ≤ 72 chars, imperative mood ("add", not "added").
- **Body**: explain *what* and *why*, wrapped at ~72 columns — skip the *how*, the diff shows that.
- **Footers**: `Fixes #...` / `Closes #...` / `BREAKING CHANGE: ...` and the mandatory `Signed-off-by:`.
- `<type>` must match the [branch prefix](#branch-naming-conventions): `feature`, `bugfix`, `hotfix`, `docs`, `refactor`, `test`, `ci`, `perf`, `chore`.

> We use **full words** (not `feat` / `fix`) so that branch prefix and commit type match character-for-character.

**Single-line commit (small change):**
```bash
git commit -s -m "bugfix/qcom_usbnet: avoid NULL deref when probe runs before sysfs init"
```

**Multi-line commit (non-trivial change):**
```
feature/qcom_usbnet: add retry logic for transient errors

Transient USB errors (-EAGAIN, -EPROTO) on noisy hubs caused the
bulk-IN endpoint to be torn down even though the device was still
responsive. Retry up to 3 times with a 50 ms backoff before giving
up; preserve the original error code if the final attempt fails.

Tested on:
- Ubuntu 24.04, kernel 6.8, SDX55 modem
- Debian 13, kernel 6.10, SDX65 modem

Fixes #123
Signed-off-by: Your Name <your.email@example.com>
```

Tips: [A Note About Git Commit Messages](http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html).

---

## Workflow Diagrams

These ASCII diagrams show how the three branch types
(`develop` / `release/x.y` / `main`) interact, and how an individual
contribution flows from your fork to upstream. Use them as a mental
model when you decide *where* to branch from and *what* to target a PR
at.

### Standard Feature / Bugfix Flow

A normal contribution. Branch from the latest `develop`, work in your
fork, open a PR to upstream `develop`. After squash-merge your branch
is auto-deleted.

```
Contributor's Fork                              Upstream (qualcomm/qcom-usb-kernel-drivers)
──────────────────                              ──────────────────────────────────────────

                                                main     ──●─────────────●──────────  (tagged: v1.2.0, v1.3.0)
                                                              ▲              ▲
                                                              │ release      │ release
                                                              │ promotion    │ promotion
                                                develop  ──●──●──●──●──●──●──●──────  (default target)
                                                                  ▲
                                                                  │ squash-merge
                                                                  │ (auto-delete branch)
fork/develop ──●── feature/qcom_usbnet/add-retry-logic ── PR ─────┘
                                                (review + CI gates)
```

### Hotfix Flow (contributor view)

A **hotfix** is an urgent fix for an **older release** that ships
without the unrelated work that has piled up on `develop` since the
release went out. The maintainer creates `release/x.y` from the
relevant tag; you, as a contributor, only act in the boxed step below.

```
Contributor's Fork                              Upstream
──────────────────                              ──────────────────────────────────────────

                                                main         ──●─────────────●──────────  v1.3.0  v1.4.0
                                                                    │
                                                                    │ (maintainer creates release/1.3
                                                                    │  from the v1.3.0 tag)
                                                                    ▼
                                                ┌── release/1.3 ────────●──────●──────────  → tag v1.3.1
                                                │                       ▲
                                                │                       │
fork/release/1.3 ──●── hotfix/dma-leak-on-disconnect ── PR ─────────────┘
                                                                        │
                                                                        │ cherry-pick fix → develop
                                                                        ▼  (maintainer)
                                                develop      ──●──●──●──●──●──●──●──●──
```

What you (the contributor) do:

1. `git fetch upstream`
2. `git checkout -b hotfix/<description> upstream/release/1.3`
   *(branch from `upstream/release/1.3` — **not** from `develop`, **not** from `main`)*
3. Make the smallest possible fix, `git commit -s`.
4. `git push -u origin hotfix/<description>`
5. Open a PR **targeting `upstream:release/1.3`** (not `main`, not
   `develop`). Then [Sync with Upstream](#sync-with-upstream) explains
   how to rebase onto `upstream/release/x.y` when the maintainer
   updates the branch.

The maintainer will take care of creating `release/x.y`, squash-merging
the PR, tagging the patch release, and cherry-picking the fix back to `develop`.

> **Why these targets?** Hotfix lands on `release/1.3`, the cherry-pick
> goes from `release/1.3` to `develop`, and `main` is never touched
> directly. This keeps the next minor release (`v1.4.0`) from regressing
> the bug.

### Submitting Your Pull Request Flow

End-to-end view of a single PR from "I want to fix something" to
"merged into `develop`". Each box is one step in the
[Pull Request Process](#pull-request-process) above.

```
                                                          Upstream:
   You (your GitHub account)                              qualcomm/qcom-usb-kernel-drivers
   ──────────────────────────                             ─────────────────────────────────

   ┌─────────────────────────┐
   │ 1. Fork the repository  │  ─────────── creates ──►   <your-username>/qcom-usb-kernel-drivers
   └────────────┬────────────┘
                │
                ▼
   ┌─────────────────────────┐
   │ 2. Clone your fork +    │
   │    add 'upstream' remote│
   └────────────┬────────────┘
                │
                ▼
   ┌─────────────────────────────────────────────┐
   │ 3. Branch from upstream/develop             │
   │    feature/qcom_usbnet/add-retry-logic      │
   └────────────┬────────────────────────────────┘
                │
                ▼
   ┌─────────────────────────────────────────────┐
   │ 4. Code + tests + docs                      │
   │ 5. git commit -s   (DCO sign-off)           │
   └────────────┬────────────────────────────────┘
                │
                ▼
   ┌─────────────────────────────────────────────┐
   │ 6. git fetch upstream                       │
   │    git rebase upstream/develop              │   (or upstream/release/x.y for hotfix)
   └────────────┬────────────────────────────────┘
                │
                ▼
   ┌─────────────────────────────────────────────┐
   │ 7. git push -u origin                       │
   │      feature/qcom_usbnet/add-retry-logic    │
   └────────────┬────────────────────────────────┘
                │
                ▼
   ┌─────────────────────────────────────────────┐
   │ 8. Open PR via GitHub UI                    │
   │    Description = .github/PULL_REQUEST_      │
   │                  TEMPLATE.md (auto-filled)  │
   └────────────┬────────────────────────────────┘
                │
                ▼
   ┌─────────────────────────────────────────────┐         ┌──────────────────────────────┐
   │ 9. CI runs: lint / build / tests / DCO      │ ──────► │ Maintainer reviews diff      │
   └────────────┬────────────────────────────────┘         │ (see Review checklist)       │
                │                                          └──────────────┬───────────────┘
                │                                                         │
                │            ◄────── "changes requested" ─────────────────┘
                │
                ▼
   ┌─────────────────────────────────────────────┐
   │ 10. Address feedback, push more commits     │
   │     (PR updates automatically)              │
   └────────────┬────────────────────────────────┘
                │
                ▼
   ┌─────────────────────────────────────────────┐         ┌──────────────────────────────┐
   │ 11. Approval                                │ ──────► │ Maintainer squash-merges     │
   └─────────────────────────────────────────────┘         │ into upstream/develop        │
                                                           │ Branch auto-deleted          │
                                                           │ Linked issue auto-closed     │
                                                           └──────────────────────────────┘
```

---

## Code Style Guidelines

The current codebase incorporates a mix of styles. **New code must follow
the conventions below**; existing code is being migrated incrementally.

General rules that apply everywhere:

- Keep changes **focused** — submit independent changes as separate PRs.
- Do not introduce unnecessary dependencies.
- Update **documentation** when behavior, options, or interfaces change.
- Add or update **tests** for any behavior change
  (see [Testing Requirements](#testing-requirements)).

### Linux Kernel Drivers (C)

Follow the canonical
[Linux kernel coding style](https://www.kernel.org/doc/html/latest/process/coding-style.html).

- Indentation: **tabs (8 columns)**, not spaces.
- Line length: ≤ 100 columns.
- Braces: K&R style; no braces on single-statement `if` / `else`.
- Naming: `snake_case` for functions and variables; `UPPER_CASE` for macros.
- Use kernel logging helpers (`dev_err`, `dev_warn`, `dev_dbg`, `pr_*`)
  rather than `printk` directly.
- Document non-trivial functions with kernel-doc comments.
- Run `scripts/checkpatch.pl` from the kernel tree on your patch
  before pushing.

### Linux User-space Applications (C / C++)

- Follow [LLVM coding conventions](https://llvm.org/docs/CodingStandards.html)
  for new code.
- Indentation: **4 spaces**, not tabs.
- Line length: ≤ 100 columns.
- Format with `clang-format` (project `.clang-format` is the source of truth).
- Lint with `clang-tidy` where configured.

### Windows Kernel Drivers (C)

- Follow Microsoft's
  [Windows Driver Kit coding guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/)
  and standard WDM/KMDF idioms.
- Indentation: **4 spaces**.
- Use the WDK / EWDK toolchain; build at warning level `/W4` and treat
  warnings as errors for new code (`/WX`).
- Use `NTSTATUS` return codes; check every status with `NT_SUCCESS`.
- Run **Static Driver Verifier (SDV)** and **Code Analysis** before
  submitting changes that touch driver code paths.

### Windows User-space Applications (C++ / C#)

- C++: follow the
  [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
  and format with `clang-format`.
- C#: follow the
  [Microsoft C# coding conventions](https://learn.microsoft.com/en-us/dotnet/csharp/fundamentals/coding-style/coding-conventions).
- Indentation: **4 spaces**.
- Enable nullable reference types (C#) and run the project's analyzers
  (Roslyn / `.editorconfig`).

### Python (Build / Tooling Scripts)

- Follow [PEP 8](https://peps.python.org/pep-0008/).
- Format with **Black** (88-column default).
- Sort imports with **isort**.
- Lint with **flake8** (and **pylint** where configured).
- Add type hints (`typing`) to public function signatures where practical.

---

## Testing Requirements

Every behavior change should be accompanied by tests, and the existing
test suite must keep passing.

### Where to find build & test instructions

- Top-level [README.md](README.md) — overall project layout.
- [src/linux/README.md](src/linux/README.md) — Linux build, install, and
  smoke-test steps. The Linux installer script handles the toolchain
  prerequisites for you.
- [src/linux/RELEASES.md](src/linux/RELEASES.md) — what changed between
  releases, useful when reproducing regressions.

### Minimum expectations before you open a PR

1. **Clean build** on the platform you changed (Linux and/or Windows).
2. **Module load / unload** sanity check on Linux and Windows
3. **Functional smoke test** — exercise the code path your change
   affects (USB enumeration, networking interface up/down, sysfs entries,
   etc.) and capture `dmesg` / `journalctl -k` output for the PR
   description if behavior changed.
4. **No new compiler warnings** introduced by your change.
5. For Windows driver changes: SDV / Code Analysis runs cleanly.

### When in doubt

Refer to the README before making your first change so you know how
the project is built and tested locally. Reviewers will ask for evidence
that you ran these steps — including the relevant log snippets in the
PR description speeds up review significantly.

---

## Review Process

- A maintainer will review your PR. Expect first response within
  ~3 business days for bug fixes and ~1 week for features.
- All required CI checks must pass: lint, build, tests.
- Reviewers may request changes; please address them or discuss in-line.
- Once approved, a maintainer will **squash-merge** (default) or **rebase-merge**
  your PR into `develop`. Your branch will then be auto-deleted.

### Things that increase the chance of acceptance
- Linked issue describing the problem.
- Small, focused diff.
- Tests that demonstrate the fix or new feature.
- Clear PR description, including *why* the change is needed.
- Up-to-date branch (rebased on latest `develop`).
- Conventional commit messages with DCO sign-off.

---

## Maintainer Responsibilities

- **Who:** Active maintainers are listed in [`AUTHORS.md`](AUTHORS.md); ping them via a GitHub Issue or PR comment.
- **Review timeline:** Response time depends on the complexity of the change — a straightforward bug fix typically gets attention sooner than a new feature or a larger refactor. The full review cycle, including feedback rounds, can take up to a month; plan accordingly and feel free to add a comment on your PR if it has not received a response in a reasonable time.
- **Communication:** All discussion happens publicly on the PR or issue — no private email for code-review matters.
- **Reviewability:** Small, focused PRs with a rebased, linear history are reviewed fastest.
- **Branch protection:** The same CI checks and approval rules apply to every PR, including those opened by maintainers — no one merges directly to protected branches.
- **DCO chain integrity:** Every commit in the stack must carry a `Signed-off-by:` trailer; a single unsigned commit blocks the PR.
- **QSDO compliance:** All PRs are subject to licence-header, dependency-review, and Semgrep scans enforced by the CI preflight workflow; unresolved findings block merge.

---

## FAQ

### Why do I have to *fork* the repository? Why not just push a branch?

Forking is the **only** way to contribute to most reputable open-source
projects, and this project follows the same model. It is required by
Qualcomm's [OSDO upstream contribution policy](https://github.qualcomm.com/pages/osdo/handbook/qcom-github/docs/upstream-github-workflows/)
and is the workflow the broader community expects.

**Concrete evidence — the upstream Linux kernel:**
> Visit `github.com/torvalds/linux` and try to edit `README` directly.
> GitHub responds with:
> *"You need to fork this repository to propose changes. Sorry, you're
> not able to edit this repository directly — you need to fork it and
> propose your changes from there instead."*
> The same repo also displays *"Pull requests cannot be created in this
> repository — the repository owner has disabled pull requests."*
> (The kernel uses `lore.kernel.org` mailing-list patches, but the
> *forking* requirement is identical to ours.)

Why every reputable open-source repo enforces this:

1. **Permissions & security** — only trusted maintainers have write
   access to the upstream repo. A malicious or accidental push could
   destabilise production code that ships to thousands of users. The
   fork model means **no one without explicit trust can write to
   `main`**, ever.
2. **Isolation** — experiments, half-finished work, force-pushes,
   broken commits, and abandoned branches stay in *your* fork. The
   upstream history stays clean, linear, and auditable.
3. **Clear review boundary** — a pull request from a fork is an
   explicit *request* asking maintainers to pull your changes. CI runs
   against your branch *before* a single byte touches upstream, and the
   maintainer reviews diff-by-diff in a controlled UI.
4. **Auditability & licensing** — each fork carries the full git history
   and the project license. This makes provenance traceable and
   protects the project (and you) legally — a critical requirement for
   Qualcomm's OSDO process and the DCO sign-off chain.
5. **Scale** — projects with hundreds or thousands of contributors
   cannot grant write access to everyone. Forks are the only model that
   scales beyond a small core team.
6. **Branch protection & CI gating** — protected `main` / `release/*`
   branches with required status checks (lint, build, tests, security,
   DCO) only work if changes flow in through PRs. PRs from forks are
   the canonical input.
7. **Compliance** — Qualcomm engineers contributing to upstream
   projects **must** submit from a personal GitHub fork (per OSDO).
   Anything else risks bypassing the OSR (Open Source Request)
   approval gate.

**TL;DR:** Forking keeps upstream safe, reviewable, auditable, and
compliant — while giving every contributor complete freedom in their
own copy. It's the universal standard for a reason: every major OSS
project (Linux, Kubernetes, Chromium, LLVM, React, …) enforces it.

### Why do we use branch names like `feature/...`, `bugfix/...`, `users/<me>/...`?

A consistent naming convention turns the branch name into a **machine-
and human-readable label**:

- **At-a-glance intent** — reviewers, maintainers and CI systems
  instantly know whether a branch is a feature, a bugfix, an urgent
  hotfix, or someone's personal experiment.
- **Identifiable owner** — `users/<username>/...` makes it obvious who
  is driving the work, which matters in shared forks and multi-team
  development.
- **Scope discipline** — naming a branch `bugfix/null-pointer-on-empty-input`
  discourages mixing an unrelated feature into the same PR. One branch = one
  logical change.
- **Automation** — release tooling (like `release-please` or
  `semantic-release`) can read the prefix (combined with Conventional
  Commit messages) to choose the next version and changelog section:
    - `feature/...` → minor version bump
    - `bugfix/...` / `hotfix/...` → patch version bump
    - `BREAKING CHANGE` footer → major version bump
- **Filtering & search** — `git branch --list 'feature/usbnet/*'` lists
  every USBNet feature branch; GitHub PR filters and dashboards work
  the same way.
- **Consistency across contributors** — without a convention, branches
  end up named `mychanges`, `temp`, `john-test-2`, etc., which tells
  reviewers nothing.

So `feature/usbnet/add-retry-logic` and `users/shasaror/usbnet-retry-experiment` are
far more useful than `mybranch` — they tell you the *type* of change,
the *area* or *owner*, and stay short enough to read at a glance.

### Do I need DCO sign-off on every commit?

Yes. Every commit in your PR must contain a `Signed-off-by:` trailer. Use
`git commit -s` (or `git rebase --signoff` to add it retroactively to a
series of commits).

### What if my PR has many small "fix typo" commits?

That's fine while you're iterating. Before merge, the maintainer will
**squash** your PR into a single clean commit on `develop`, so the messy
history will not appear in the project log.

### My PR has conflicts — should I use GitHub's "Resolve conflicts" button?

**No.** Resolve conflicts **locally with `git rebase`**, never via
GitHub's web *Resolve conflicts* button.

#### What the GitHub web button actually does

When GitHub shows *"This branch has conflicts that must be resolved"*,
the **Resolve conflicts** button opens an in-browser editor. Click
*Mark as resolved → Commit merge*, and GitHub creates a **merge commit**
on top of your PR branch with two parents:

- parent 1 = the tip of your branch
- parent 2 = the current tip of `develop`

That `Merge branch 'upstream/develop' into feature/...` commit is
exactly the thing our review checklist forbids ("**Branch is rebased
onto the latest target — no merge commits in the PR**"). A reviewer
will close such a PR or ask you to clean it up before re-opening.

#### Why we don't allow web-UI conflict resolution

1. **It always creates a merge commit.** The web UI has no rebase
   option. Our project goal is a **linear history** on `develop`
   (Maintainer Responsibility #10), and squash-merge requires a
   fast-forwardable PR.
2. **It doesn't run your tests.** You resolved the conflict in a
   browser; you didn't compile, didn't load the module, didn't run
   the test suite. You're shipping an untested merge resolution.
3. **It breaks the DCO sign-off chain.** GitHub's auto-merge commit
   doesn't carry your `Signed-off-by:` trailer the way your hand-
   written commits do; the future DCO bot then complains about the
   merge commit and the PR check goes red.
4. **It defeats CI's purpose.** CI re-runs against the merge result,
   but if there were *semantic* conflicts (e.g. the other PR renamed
   a function your code calls), you wouldn't have noticed locally —
   and now `develop` is broken once your PR lands.

#### What contributors should do instead

Resolve conflicts **locally**, by rebasing onto the latest target:

```bash
git fetch upstream
git checkout feature/qcom_usbnet/add-retry-logic
git rebase upstream/develop

# git stops on each conflicting commit:
#   CONFLICT (content): Merge conflict in src/linux/...
# edit the file(s) to resolve, then:
git add <resolved-file>
git rebase --continue

# repeat until rebase finishes, then:
git push --force-with-lease
```

This produces **no merge commit** — your commits are *replayed* onto
the new `develop`, with the conflict-resolution edits folded into the
relevant commit. The PR diff stays clean, your tests run locally on
the new base, the DCO sign-off survives, and the maintainer can
squash-merge as a fast-forward.

`--force-with-lease` is the safe variant of `--force`: it refuses to
overwrite the remote if someone else pushed to your branch in the
meantime. If something goes wrong mid-rebase, `git rebase --abort`
puts you back where you started — rebase is fully reversible.

For hotfix branches, replace `upstream/develop` with the relevant
`upstream/release/x.y` (never `upstream/main`).

#### Quick decision table

| Situation | What to do |
|---|---|
| GitHub says *"This branch has no conflicts"* | Nothing — you're good to go. |
| GitHub says *"This branch is out-of-date"* but no conflicts | Rebase locally onto `upstream/develop` and force-push (saves CI from re-running on a stale base). |
| GitHub says *"This branch has conflicts that must be resolved"* | Rebase locally onto `upstream/develop`, resolve conflicts, force-push. **Do not** click the GitHub *Resolve conflicts* button. |
| GitHub *Resolve conflicts* button is the only option visible? | Wrong PR target — your branch may have been opened against `main` instead of `develop`. Close, rebase, re-open with the correct target. |

### Where do I report a security issue?

Please **do not** open a public GitHub issue for security vulnerabilities.
See [SECURITY.md](SECURITY.md) for the responsible disclosure process.

---

Pat yourself on the back and wait for your pull request to be reviewed —
and thank you for contributing! 🎉
