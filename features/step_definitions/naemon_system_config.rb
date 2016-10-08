Given(/^I have the following configuration in naemon$/) do |table|
  table.hashes.each do |obj|
    @naemonsysconfig.set_var(obj["name"], obj["value"])
    puts "Adding system config #{obj["name"]}=#{obj["value"]}"
  end
end

Given(/^I have naemon config ([^ ]+) set to (.+)$/) do |name, value|
  @naemonsysconfig.set_var(name, value)
end

Given(/^I have naemon system config file ([^ ]+)$/) do |filename|
  step "I have config file #{filename}", @naemonsysconfig.configfile
end
