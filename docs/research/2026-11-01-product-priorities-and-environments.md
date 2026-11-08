# PS5Term product priorities and optional environments

> Historical research snapshot, not a current feature or deployment guide.
> Start with [development scope](../DEVELOPMENT_SCOPE.md) and
> [current feature access](../FEATURES.md); retain the dated evidence below.

User clarification, 2026-11-01. This supplements the platform investigation and supersedes any interpretation that makes a VM the main product.

## Product order

Standalone startup is a requirement across these priorities: once the jailbreak is active, PS5Term must own its helpers and optional services. Users must not have to send another payload or start an external ELF loader. See [the tool survey and integration boundary](2026-11-01-ps5-tools-and-standalone-integration.md).

1. A high-quality native terminal app: capable terminal emulation, readable rendering, history/search, selection/copy/paste, sessions, profiles and discoverable controls.
2. Complete usability with a controller alone; keyboard-only and keyboard-plus-mouse are equally supported configurations. No operation should require a mouse. Keyboard and mouse implementation belongs to the other active agent.
3. An expanding collection of PS5-compatible native utilities, libraries and runtimes, built reproducibly with explicit compatibility records.
4. Settings for optional SSH service, persistent connection/authentication configuration, and optional additional environments.
5. Additional environments extend the terminal; they do not redefine it as a Linux launcher.

## Terminal interaction requirements

- Keep terminal emulation, rendering, session management and input-device integration separate. The frontend must be able to display native, jail, VM or remote sessions without changing its basic behavior.
- Distinguish app navigation from input delivered to a running terminal application. A controller “Run” action can submit an editable command, but must not inject an extra newline into an arbitrary full-screen program. Closing a menu and interrupting a process need distinct actions.
- A controller-accessible palette must make Escape, Tab, arrows, modifiers, interrupt, end-of-input, paste and session switching available without a physical keyboard. Show contextual hints instead of permanently occupying terminal rows with setup instructions.
- Keyboard-only users need shortcuts for every app action. Mouse users gain pointing/selection; no core operation depends on it. Hot-plugging devices must preserve the active session and typed input.
- Separate app selection/scrollback from terminal-application mouse reporting. Define an explicit override for selecting text when an application has requested mouse events.
- Measure font readability at TV viewing distance, input latency, scrolling performance and long-session memory usage. Correct escape sequences and interactive behavior precede decorative effects.
- Session profiles retain font/theme, working directory, environment variables, backend and connection settings. Command blocks and completion are optional shell integrations, not assumptions imposed on all byte streams.

These are integration requirements for the product design, not changes to the other agent's keyboard/mouse implementation.

## Environment choices

| Environment | What it provides | What it cannot provide by itself |
| --- | --- | --- |
| PS5 native package prefix | Host research and a growing native Unix toolset | A different kernel or generic distribution binary compatibility |
| Native jail, if supported | Isolated processes/filesystem sharing the Sony kernel | A vanilla FreeBSD kernel or Linux syscall compatibility |
| FreeBSD VM | Complete standard FreeBSD guest kernel/userspace | Automatic access to host PS5 internals |
| Linux VM | Complete Linux guest kernel/userspace and normal Linux packages | Native-speed execution without a working accelerator |
| Remote environment over SSH | Complete environment on an existing server | Local PS5 execution |

Recommendation: expose a consistent **Environments** concept in Settings and session profiles. Investigate native jails as a lightweight optional backend; retain a real guest VM as the path to a complete vanilla OS. Never label an ordinary package prefix or chroot as an isolated container.

## What the FreeBSD container projects teach us

[FreeBSD jails](https://docs.freebsd.org/en/books/handbook/jails/) share the host kernel. Their viability on PS5 requires evidence for the actual jail syscalls, allowed parameters, filesystem mounting, credentials, process attachment and networking. Modern FreeBSD base binaries must also match the host ABI; unpacking a new release is insufficient.

[Bastille](https://bastillebsd.org/) offers declarative, Dockerfile-like templates for jailed applications. Its current site specifies FreeBSD 14+, so running it unchanged on Sony's FreeBSD-derived system is not an established option. Borrow its template and environment lifecycle ideas after measuring host capabilities.

[Pot](https://pot.pizzamig.dev/) is another jail/container manager. Its full platform assumptions, including storage and network requirements, must be audited before any port. Installing a manager cannot add the kernel features it manages.

For a Docker-like user experience, useful features are reproducible image recipes, named environments, start/stop, storage volumes, networking settings, export/import and an “open terminal” action. These can sit over a VM backend even when native jails are unavailable. Running Docker Engine inside a Linux guest is a later compatibility/resource test, dependent on guest kernel configuration; it is not a prerequisite for environments.

## First evidence to gather

- Native terminal: VT engine choice, process lifecycle and PTY availability.
- Native packages: stable installation prefix, package manifests and a small tool collection.
- Jails: inspect available syscall/API definitions; schedule a small capability probe separately from the other agent's console tests. Presence of SDK declarations is not sufficient.
- VM: reproducible headless boot, networking, persistent disk, responsive terminal and reliable shutdown.

The Mac lab prepares normal Linux and FreeBSD reference environments. It does not establish native PS5 jail support or VM performance on the console.
