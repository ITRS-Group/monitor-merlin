# This step makes it possible to do:
#   Given for 3 times I send command SOMETHING
#
# Passing the "for 3 times" at the end makes it ambigous for steps having a
# whidcard at the end, for example if SOMETHING has speces within
Given(/^for ([\d]+) times (.*)$/) do |count, stepdef, *extra|
  for i in 1..count.to_i
    step stepdef, *extra
  end
end