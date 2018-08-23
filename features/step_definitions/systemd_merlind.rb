# must be run as root with merlind installed
Given(/^I start merlind with systemd$/) do
    ret = system("systemctl start merlind")
    if (ret == false)
        fail("Failed to start merlind with systemd")
    end
end


Then(/^merlind should not run as the root user$/) do
    # verify merlind is not running as root user
    # also fails if merlind is not running at all
    ret = system("ps aux | grep merlind | grep -v grep | grep -v root")
    if (ret == false)
        fail("Merlind runs as root user or not at all")
    end
end
