# Leak guard

These hooks block commits and pushes that contain personal or machine-specific data. `pre-commit` scans the staged diff. `pre-push` scans every commit the push sends, including its message, so commits made with `git commit --no-verify` are still caught at push.

Each finding prints only its rule and location. A `credential` finding names the staged changes or the pushed ref, so rerun `gitleaks` with `--verbose` to find the file and line.

| Rule | Blocks |
| --- | --- |
| `profile-path` | User profile paths on Windows, macOS, and Linux |
| `drive-path` | Any drive-rooted path |
| `email` | Email addresses, except `noreply` addresses |
| `identity` | The current username, computer name, git `user.name` and each word of three or more characters in it, and `user.email`, in any case |
| `denylist` | Terms from your private denylist file, in any case |
| `credential` | Secrets, through [gitleaks](https://github.com/gitleaks/gitleaks) |

## Setup

The guard needs PowerShell 7 (`pwsh`) and `gitleaks` on `PATH`. While `gitleaks` or the denylist is missing, it blocks every commit and push.

Run these once in each clone.

```sh
git config core.hooksPath .githooks
git config --local leakguard.denylist <path>
```

The denylist file holds one term per line. The guard skips blank lines and lines starting with `#`.

You can point `leakguard.allowlist` at a file of exact strings that may appear, such as your public handle. It uses the denylist's file format.

The guard removes each allowed string (case-sensitive) from a line before the `identity` and `denylist` rules test it, so the same term elsewhere still blocks.

A configured allowlist file that is missing blocks every commit and push.

```sh
git config --local leakguard.allowlist <path>
```
