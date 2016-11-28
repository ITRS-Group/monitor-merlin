Given(/^I have merlin config ([^ ]+) set to (.+)$/) do |name, value|
  @merlinnodeconfig.set_var(name, value)
end
