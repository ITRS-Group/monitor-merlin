handler_desc='Checks all locally mounted filesystems for low amounts of free inodes.'
handler_exec()
{
  PATH="/opt/plugins:$PATH"
  depchk check_disk

  check_disk -W 10% -K 5% -L -A -v
}
