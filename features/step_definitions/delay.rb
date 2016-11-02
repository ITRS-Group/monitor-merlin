Given /^I wait for (\d+) seconds?$/ do |n|
  sleep(n.to_i)
end