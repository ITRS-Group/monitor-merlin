Given(/^I have naemon config ([^ ]+) set to (.+)$/) do |name, value|
  @naemonsysconfig.set_var(name, value)
end

Given(/^I have naemon system config file ([^ ]+)$/) do |filename|
  step "I have config file #{filename}", @naemonsysconfig.configfile
end
