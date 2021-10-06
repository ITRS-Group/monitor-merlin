# Changelog
All notable changes to this project will be documented in this file. Changes to
vendor specific packaging are excluded.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Calendar Versioning](https://calver.org/), with the
format YYYY.MM.MICRO. The tag may occasionally be made just prior to the change
of month.

## [Unreleased]
### Fixed
- OBS Build now correctly `Requires: python2-PyMySQL` instead of `BuildRequires`
  on EL8. This fixes a couple of mon scripts for example
  `mon status install-time`

## [2021.10.1] - 2021-10-01
### Added
- Community packages for CentOS/RHEL 7 & 8 are now generated with Open Suse
  Build Service
- Changelog has been added!
- Configure option `--with-ls-socket` allows one to configure where the
  livestatus socket is expected to be for the apps.
- For slim packages, the `merlin_cluster_tools` script has been added. This
  script can be used with the `cluster_update` setting to automatically handle
  clustering.

### Changed
- Paths in the `mon` apps has been adjusted to configurable values. They are
  either fetched directly from the Naemon config, or from values set when
  running `configure`. This should make the `mon` apps less OP5 specific and
  work correctly with community installs.
- The user/group in the systemd and logrotate files has been adjusted to match
  the values set with `configure`.

### Removed
- `mon log fetch` and `mon log sortmerge` has been removed, as these are broken
   and no longer used.

### Fixed
- Correctly tokenize `ipc_blocked_hostgroups` so that the setting works as it
  should with multiple hostgroups defined.
- `mon db fixindexes` had an outdated OP5 specific paths to SQL files. The path
  has been correctly to work on both OP5 and community packages using the
  configure option `--datarootdir`.

## [2021.9.1] - 2021-09-03
### Removed
- Sample cronjobs in /apps

## [2021.8.1] - 2021-07-30
### Added
- `--write` option to `mon merlinkey generate` which will write the path to the
  newly generated private encryption key to merlin.conf
- New mon commands: slimpoller. These are inteded for slim pollers to register
  or deregister with a master node.
- Implemented `ipc_blocked_hostgroups` setting. This setting will allow to block
  the specific node from executing checks from the specified hostgroups

### Changed
- Exceptions added to the `auto_delete` functionality to avoid master takeover
  of checks. Don't auto delete in pollergroups with only one node, and when
  no nodes have been heard from recently.
- Only enable `auto_delete` for pollers.
- Make `mon merlinkey generate` idempotent.

### Fixed
- Fix an issue that caused `auto_delete` nodes to be removed prematurely.
- Show the correct time to node auto deletion in the debug log.
- Correctly set the required Naemon version in the build tools.


## [2021.4.1] - 2021-04-01
### Added
- `mon oconf fetch` now support file sync via the --sync command. Ported to
   python
- Support for SSH-less "test this check" using a new query handler `runcmd`
- Node identification is now possible using UUID, by using the new merlin.conf
  options `ipc_uuid` and `uuid`.
- Allow setting `publickey` with `mon node add`
- New setting `cluster_update` allows an arbitary script to be run when a remote
  node signals the cluster config is invalid. The script chosen should be able
  adjust the cluster config into a valid state.
- `mon oconf remote-fetch` added, which will tell a remote node to execute
  `mon oconf fetch`.
- `auto_delete` functionality allows removing nodes which have not been seen
  withing x seconds.

### Changed
- Add support for Naemon 1.2.4. Merlin now requires Naemon version >= 1.2.4.

## [2021.2.1] - 2021-01-29
### Changed
- Binlogs are now stored persistently to file when merlin shuts down. The
  binlog is loaded again on startup ensuring no events are lost if merlin is
  restarted while another node is down. Can be disabled by setting
  `binlog_persist = 0`.

Changelogs for earlier versions are not kept, please see git log for details.
