# OwlSwitch Documentation

Choose the guide for the task. Run shell commands from the repository root
unless a guide says otherwise.

| Guide | Owns |
|---|---|
| [Install](INSTALL.md) | Requirements, installation, updates, data migration, and uninstalling. |
| [Architecture](ARCHITECTURE.md) | Module behavior, shared playback and QML patterns, settings schema, and data inventory. |
| [Building](BUILDING.md) | Setup, build/run commands, automated checks and canaries, packaging, release gates, and recovery. |
| [Contributing](CONTRIBUTING.md) | Contribution principles, code style, review expectations, and manual media checks. |
| [Security](SECURITY.md) | Vulnerability reporting, credential handling, privacy, and distribution trust requirements. |
| [Changelog](CHANGELOG.md) | Dated, user-facing release history. |
| [Roadmap](ROADMAP.md) | Future priorities. |

The root [README](../README.md) introduces the product, and [AGENTS.md](../AGENTS.md)
contains repository-wide agent instructions. Keep detailed procedures in their
owning guide and link to them from the entry points. When moving a document,
update incoming links, workflow paths, and tests that read it.

## Plans and Investigations

Plans preserve scoped decisions and evidence. Each record states its status;
the guides above describe the maintained implementation.

| Record | Scope |
|---|---|
| [Separate-screen video and focus](plans/video-fullscreen-focus-plan.md) | Window/focus implementation and regression evidence. |
| [Nature audio, 1.6.6](plans/nature-earth-garden-release-plan.md) | Release decisions and recorded acceptance. |
| [Upstream review, 1.6.5](plans/upstream-sync-1.6.5.md) | Selective upstream integration and deferred work. |

## Subsystem Guides

Keep operational instructions beside the code they describe:

- [Native display regression tests](../tests/native_display/README.md): virtual
  monitors, real-window assertions, prerequisites, evidence, and limitations.
- [Diagnostics relay](../diagnostics-relay/README.md): the separately deployed
  reporting Worker, configuration, and local verification.
