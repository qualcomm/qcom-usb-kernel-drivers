<!--
Thank you for contributing to Qualcomm Open Source Project!
Please read CONTRIBUTING.md before submitting:
https://github.com/qualcomm/qcom-usb-kernel-drivers/blob/develop/CONTRIBUTING.md

Title format (Conventional Commits, full words):
  <type>/<scope>: <short summary>
Examples:
  feature/qcom_usbnet: add retry logic for transient errors
-->

## Description

<!--
Describe **what** this PR changes and **why**. Bullet list is fine.
-->

Fixes #<issue-number>

## Type of Change

<!-- Tick all that apply by replacing [ ] with [x]. -->

- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Hotfix (urgent fix targeted at a `release/x.y` branch)
- [ ] Refactor (no functional change)
- [ ] Performance improvement
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update
- [ ] Test-only change
- [ ] CI / build-pipeline change

## Target branch

<!-- Tick the branch this PR targets. -->

- [ ] `develop` (default for `feature/`, `bugfix/`, `docs/`, `ci/`)
- [ ] `release/x.y` (only for `hotfix/` branches)

## How has this been tested?

<!--
Describe the tests you ran. Reviewers will ask for evidence — paste the
relevant snippets here.
-->

## Checklist

<!-- Replace [ ] with [x] for each item that's done. PRs missing items will be sent back for changes. -->

- [ ] **All required CI checks are green** on the latest commit of this PR
- [ ] **Build succeeds** following the steps in the [README](../README.md) / [src/linux/README.md](../src/linux/README.md)
- [ ] My code follows the [Code Style Guidelines](../CONTRIBUTING.md#code-style-guidelines) of this project
- [ ] My branch follows the [naming convention](../CONTRIBUTING.md#branch-naming-conventions): `<branch-prefix>/<area>/<description>`
- [ ] PR title follows the Conventional Commits format with our full-word types (`feature` / `bugfix` / `hotfix` / `docs` / …)
- [ ] I have performed a **self-review** of my own code
- [ ] I have added **code comments** in areas that are complex or hard to understand
- [ ] My changes generate **no new compiler warnings**
- [ ] I have added **tests** for my fix or feature (or noted why tests are not feasible)
- [ ] New and existing **tests pass locally** with my changes
- [ ] My branch is **rebased onto the latest `develop`** (or `release/x.y` for hotfix) — no merge commits
- [ ] Every commit has `Signed-off-by:` (DCO) — see [CONTRIBUTING.md](../CONTRIBUTING.md#commit-message-guidelines)
- [ ] I have **linked** the relevant issue if any (`Fixes #...`)
- [ ] Any dependent changes have been merged and published in downstream modules