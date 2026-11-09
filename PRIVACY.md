# Privacy and public contributions

Keep personal names, email addresses, usernames, hostnames, local home paths,
credentials, and device identifiers out of commits and issue reports. Review
text, screenshots, and generated artifacts before sharing them.

Inspect the staged files and Git identity before a contribution:

```sh
git diff --cached --check
git diff --cached
git status --short
git var GIT_AUTHOR_IDENT
git var GIT_COMMITTER_IDENT
```

Use an appropriate non-identifying Git name and email for public work. A GitHub
noreply address may still associate a commit with a profile. Repository owner
names and public GitHub activity remain visible.

Ignore rules reduce accidental additions. They do not redact terminal output,
issue comments, release assets, or files deliberately staged with `git add -f`.
Removing a file in a later commit does not erase previous Git history.

## Calculator data

`nsptool info` prints the calculator's unique electronic ID. Redact it before
sharing output. Directory listings, transferred documents, backup contents, and
local paths can reveal personal information. Use synthetic filenames and data
for issue reports whenever possible.

Keep backups outside the source checkout or under an ignored location.
Default `nspire-backup-*` directories, document files, OS packages, downloads,
and build products are ignored. A manually chosen backup directory may not
match those rules. Never commit a calculator backup, support-log archive, or
vendor OS image to demonstrate a bug.

A useful report includes the calculator model, OS version, distribution,
architecture, libnspire revision, command shape, and a short redacted error.
The electronic ID and personal documents are not needed for an initial report.
