# This step makes it possible to do:
#   Given for 3 times I send command SOMETHING
#
# Passing the "for 3 times" at the end makes it ambigous for steps having a
# wildcard at the end, for example if SOMETHING can contain spaces
Given(/^for ([\d]+) times (.*)$/) do |count, stepdef, *extra|
  for i in 1..count.to_i
    step stepdef, *extra
  end
end