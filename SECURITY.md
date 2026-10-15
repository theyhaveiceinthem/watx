# Security Policy

watx parses WebAssembly text and binary modules and can execute modules with an
interpreter or a native baseline JIT. The implementation and its native target
paths are new and have not been verified across all supported platforms. Do not
assume that watx provides a security boundary or is safe for untrusted modules.

## Reporting a vulnerability

Please do not publish exploit details, proof-of-concept payloads, or sensitive
data in a public issue.

If private vulnerability reporting is enabled for this GitHub repository, use
the repository's **Security** tab and choose **Report a vulnerability**. If
that option is unavailable, open a public issue asking for a private reporting
route, without including exploit details or sensitive data. Use a private route
already published by the project if one becomes available.

There is no published response-time or remediation guarantee. Please include
the affected commit or release, operating system and architecture, and a concise
description of the impact when reporting privately.

## Supported versions

The project has not published a version support policy. Reports against the
current repository state are useful; include the commit or release identifier
so the issue can be reproduced against the same code.
