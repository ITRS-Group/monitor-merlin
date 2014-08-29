handler_desc='Verifies that the op5 license file is installed and valid.'
handler_exec()
{
  lic_file='/etc/op5license/op5license.xml'

  [ -f "$lic_file" ] || \
    dieplug '2' "No op5 license has been installed."
  [ -r "$lic_file" ] || \
    dieplug '3' "Unable to read op5 license file ($lic_file)."

  PATH="/opt/op5sys/bin:$PATH"
  depchk op5license-verify

  if op5license-verify "$lic_file" &> /dev/null; then
    dieplug '0' 'op5 license file valid.'
  else
    dieplug '2' 'op5 license file invalid.'
  fi
}
