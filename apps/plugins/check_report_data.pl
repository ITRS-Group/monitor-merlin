#!/usr/bin/perl -w

use strict;
use DBI;
use DBD::mysql;

my $db_host = "localhost";
my $db_user = "root";
my $db_pass = "";
my $db_db   = "monitor_reports";
my $db_conn = DBI->connect("dbi:mysql:$db_db:$db_host:3306", $db_user);

my $time_limit = 90000; #Seconds
my $critical = 0;
my $num_rows; #The result from the query will be placed here.

my $db_query = "select count(timestamp) 
				from report_data
				where timestamp > unix_timestamp(now()) - $time_limit
					order by timestamp 
					desc limit 1";

my $db_query_handle = $db_conn->prepare($db_query);
   $db_query_handle->execute();

$db_query_handle->bind_columns(undef, \$num_rows);
$db_query_handle->fetch();


if ($num_rows > 0) {
	print "OK: The data in report_data is ok. $num_rows lines found\n";
	exit 0;
} else {
	print "CRITICAL: The data in report_data is too old!\n";
	exit 2;
}

exit 3;
