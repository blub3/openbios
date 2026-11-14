# Publishing this work as a credited GitHub fork

## Recommended repository identity

Use GitHub's **Fork** action on https://github.com/osx360/openbios under your own
account. Retain the inherited Git history. Suggested description:

> Unofficial experimental osx360/OpenBIOS fork: Xenon EHCI storage, OHCI keyboard fallback and tested Tiger SSD autoboot.

Do not describe this as the original Xbox 360 port or an official osx360 release.
The README links to both immediate upstream and the original OpenBIOS project.

## Before the first push

Review `git status --short` and the full diff. Keep the source, tests,
documentation and existing copyright/license notices. Do not stage generated
`bootstrap.dict`, `obj-*` output, USB-stick backups, installed OS files,
`Xbox360.mkext`, SSH material or raw diagnostic screenshots. Some historical
screenshots expose console CPU/DVD keys and personal information; use redacted
copies only after a separate review.

Run:

```sh
git diff --check
python3 tests/ehci-transfer-test.py
python3 tests/usb-keyboard-init-test.py
python3 tests/ehci-handoff-test.py
```

In a ptrace-based sandbox, LeakSanitizer may refuse to run. For that environment,
prefix each test command with `ASAN_OPTIONS=detect_leaks=0`; address/undefined
behavior checks remain enabled, but leak checking is skipped. All three host
simulations passed with this setting during preparation on 2026-11-14.

Review the Git author identity before making public commits. Commit the reviewed
implementation and documentation on a branch such as `codex/usb2-ssd-boot`.
Keep the inherited history; do not create a fresh repository containing an
unattributed snapshot of the upstream source.

## Connect the fork

The local repository is prepared with `upstream` pointing to **osx360/openbios**
and `origin` pointing to **blub3/openbios**. The public fork must be created
before pushing. After using GitHub's Fork action on the upstream repository,
verify the remotes and push the prepared branch:

```sh
git remote -v
git push -u origin codex/usb2-ssd-boot
```

Check that the push destination is your fork. The prepared origin URL is `https://github.com/blub3/openbios.git`;
adjust it if you choose another repository name. Make the development
branch visible in the fork's README/default branch settings as appropriate.
A later upstream pull request can offer focused changes with the hardware
results and limitations attached; it has not been submitted by this preparation.

## GitHub Actions and binary releases

Inherited workflows are present in `.github/workflows/`. Inspect and enable
Actions deliberately in the fork. The builder workflow can publish a container;
the release workflow publishes binaries on `v*` tags and currently marks them
as non-prereleases. The build workflow's `*` branch pattern may need `**` if
builds should run on slash-containing development branches.

**Prepare source publication first. Do not use the inherited release workflow
unchanged for this experimental binary release.** Before tagging a release:

- Mark the release experimental/prerelease and document the tested Xbox revision.
- Bundle the exact corresponding source, build instructions, root `COPYING`
  and applicable third-party notices, including the libpayload-derived notices.
  The inherited binary-only ZIP recipe does not currently assemble these materials.
- Record the commit, builder image digest and ELF SHA-256; do not identify a
  build only by its potentially stale compiled timestamp.
- Describe the fixed `ud1` default, recovery ELF, cancellation behavior and
  outstanding networking/DVD-installation limitations.
- Include only binaries built from this repository whose provenance has been
  checked; do not attach Apple system files or a cloned SSD image.

No GitHub repository, push, release or upstream pull request is created merely
by following the local documentation preparation step.
