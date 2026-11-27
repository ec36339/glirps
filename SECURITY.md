# Security Policy

## Supported versions

Security fixes are made on `develop`. Older tags and commits do not get separate fixes.

## Reporting a vulnerability

**Please do not report a new vulnerability in a public issue.**

[Report it privately on GitHub](../../security/advisories/new).

Please include:

- the affected tag or commit
- your platform, compiler, and build type
- the steps or a small program that reproduces the problem
- what an attacker would need to control, and what could happen

Leave out personal data, passwords, tokens, and other secrets.

This project is maintained in spare time, so no response or fix time is guaranteed.

## What to report privately

Examples include:

- memory access out of bounds, or other memory safety errors
- a crash, hang, or uncontrolled output that untrusted data could cause
- a problem in the build, CI, or published documentation that could let someone run or distribute unwanted code

Report ordinary bugs, such as a sound that is wrong but harmless, with the [issue form](../../issues/new?template=issue.yml). If you are unsure, report privately.

## Untrusted input

Do not assume that GLIRPS safely handles sound programs from untrusted sources. Check such data in your own program. Known problems are listed in the [issue tracker](../../issues).
