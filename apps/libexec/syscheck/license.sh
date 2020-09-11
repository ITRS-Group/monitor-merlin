handler_desc='Verifies that the OP5 license file is installed and valid for more than 14 days.'
handler_exec()
{
  /opt/plugins/check_op5_license -T d -w 14 -c 14 &> /dev/null
  if [ $? -eq 0 ]; then
    dieplug '0' 'OP5 license check reports license valid for >14 days.'
  else
    dieplug '2' 'OP5 license check reports license invalid, or valid for less than 14 days!'
  fi
}
