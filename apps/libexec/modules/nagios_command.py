import signal, time, os, signal, posix, errno

nagios_command_pipe_open_failed = False

def _cmd_pipe_sighandler(one, two):
	global nagios_command_pipe_open_status

	nagios_command_pipe_open_failed = True
	return False


class nagios_command:
	command_info = {
		'ADD_HOST_COMMENT': {
			'nagios_id': 1,
			'description': 'This command is used to add a comment for the specified host.  If you work with other administrators, you may find it useful to share information about a host that is having problems if more than one of you may be working on it.  If you do not check the \'persistent\' option, the comment will be automatically be deleted at the the next program restarted.',
			'brief': 'You are trying to add a host comment',
			'template': 'ADD_HOST_COMMENT;host_name;persistent;author;comment',
		},
		'DEL_HOST_COMMENT': {
			'nagios_id': 2,
			'description': 'This command is used to delete a specific host comment.',
			'brief': 'You are trying to delete a host comment',
			'template': 'DEL_HOST_COMMENT;comment_id',
		},
		'ADD_SVC_COMMENT': {
			'nagios_id': 3,
			'description': 'This command is used to add a comment for the specified service.  If you work with other administrators, you may find it useful to share information about a host or service that is having problems if more than one of you may be working on it.  If you do not check the \'persistent\' option, the comment will automatically be deleted at the next program restart.',
			'brief': 'You are trying to add a service comment',
			'template': 'ADD_SVC_COMMENT;service;persistent;author;comment',
		},
		'DEL_SVC_COMMENT': {
			'nagios_id': 4,
			'description': 'This command is used to delete a specific service comment.',
			'brief': 'You are trying to delete a service comment',
			'template': 'DEL_SVC_COMMENT;comment_id',
		},
		'ENABLE_SVC_CHECK': {
			'nagios_id': 5,
			'description': 'This command is used to enable active checks of a service.',
			'brief': 'You are trying to enable active checks of a service',
			'template': 'ENABLE_SVC_CHECK;service',
		},
		'DISABLE_SVC_CHECK': {
			'nagios_id': 6,
			'description': 'This command is used to disable active checks of a service.',
			'brief': 'You are trying to disable active checks of a service',
			'template': 'DISABLE_SVC_CHECK;service',
		},
		'SCHEDULE_SVC_CHECK': {
			'nagios_id': 7,
			'description': 'This command is used to schedule the next check of a service.  The check will be re-queued to be run at the time you specify. If you select the <i>force check</i> option, a service check will be forced regardless of both what time the scheduled check occurs and whether or not checks are enabled.',
			'brief': 'You are trying to schedule a service check',
			'template': 'SCHEDULE_SVC_CHECK;service;check_time',
		},
		'DELAY_SVC_NOTIFICATION': {
			'nagios_id': 9,
			'description': 'This command is used to delay the next problem notification that is sent out for the specified service.  The notification delay will be disregarded if the service changes state before the next notification is scheduled to be sent out.  This command has no effect if the service is currently in an OK state.',
			'brief': 'You are trying to delay a service notification',
			'template': 'DELAY_SVC_NOTIFICATION;service;notification_delay',
		},
		'DELAY_HOST_NOTIFICATION': {
			'nagios_id': 10,
			'description': 'This command is used to delay the next problem notification that is sent out for the specified host.  The notification delay will be disregarded if the host changes state before the next notification is scheduled to be sent out.  This command has no effect if the host is currently UP.',
			'brief': 'You are trying to delay a host notification',
			'template': 'DELAY_HOST_NOTIFICATION;host_name;notification_delay',
		},
		'DISABLE_NOTIFICATIONS': {
			'nagios_id': 11,
			'description': 'This command is used to disable host and service notifications on a program-wide basis.',
			'brief': 'You are trying to disable notifications',
			'template': 'DISABLE_NOTIFICATIONS',
		},
		'ENABLE_NOTIFICATIONS': {
			'nagios_id': 12,
			'description': 'This command is used to enable host and service notifications on a program-wide basis.',
			'brief': 'You are trying to enable notifications',
			'template': 'ENABLE_NOTIFICATIONS',
		},
		'RESTART_PROCESS': {
			'nagios_id': 13,
			'description': 'This command is used to restart the program. Executing a restart command is equivalent to sending the process a HUP signal. All information will be flushed from memory, the configuration files will be re-read, and monitoring will start with the new configuration information.',
			'brief': 'You are trying to restart the monitoring process',
			'template': 'RESTART_PROCESS',
		},
		'SHUTDOWN_PROCESS': {
			'nagios_id': 14,
			'description': 'This command is used to shutdown the monitoring process. Note: Once the process has been shutdown, it cannot be restarted via the web interface!',
			'brief': 'You are trying to shutdown the monitoring process',
			'template': 'SHUTDOWN_PROCESS',
		},
		'ENABLE_HOST_SVC_CHECKS': {
			'nagios_id': 15,
			'description': 'This command is used to enable active checks of all services associated with the specified host.  This <i>does not</i> enable checks of the host unless you check the \'Enable for host too\' option.',
			'brief': 'You are trying to enable active checks of all services on a host',
			'template': 'ENABLE_HOST_SVC_CHECKS;host_name',
		},
		'DISABLE_HOST_SVC_CHECKS': {
			'nagios_id': 16,
			'description': 'This command is used to disable active checks of all services associated with the specified host.  When a service is disabled it will not be monitored.  Doing this will prevent any notifications being sent out for the specified service while it is disabled.  In order to have the service check scheduled again you will have to re-enable the service. Note that disabling service checks may not necessarily prevent notifications from being sent out about the host which those services are associated with.  This <i>does not</i> disable checks of the host unless you check the \'Disable for host too\' option.',
			'brief': 'You are trying to disable active checks of all services on a host',
			'template': 'DISABLE_HOST_SVC_CHECKS;host_name',
		},
		'SCHEDULE_HOST_SVC_CHECKS': {
			'nagios_id': 17,
			'description': 'This command is used to scheduled the next check of all services on the specified host.  If you select the <i>force check</i> option, a check of all services on the host will be performed regardless of both what time the scheduled checks occur and whether or not checks are enabled for those services.',
			'brief': 'You are trying to schedule a check of all services for a host',
			'template': 'SCHEDULE_HOST_SVC_CHECKS;host_name;check_time',
		},
		'DELAY_HOST_SVC_NOTIFICATIONS': {
			'nagios_id': 19,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DELAY_HOST_SVC_NOTIFICATIONS;host_name;notification_time',
		},
		'DEL_ALL_HOST_COMMENTS': {
			'nagios_id': 20,
			'description': 'This command is used to delete all comments associated with the specified host.',
			'brief': 'You are trying to delete all comments for a host',
			'template': 'DEL_ALL_HOST_COMMENTS;host_name',
		},
		'DEL_ALL_SVC_COMMENTS': {
			'nagios_id': 21,
			'description': 'This command is used to delete all comments associated with the specified service.',
			'brief': 'You are trying to delete all comments for a service',
			'template': 'DEL_ALL_SVC_COMMENTS;service',
		},
		'ENABLE_SVC_NOTIFICATIONS': {
			'nagios_id': 22,
			'description': 'This command is used to enable notifications for the specified service.  Notifications will only be sent out for the service state types you defined in your service definition.',
			'brief': 'You are trying to enable notifications for a service',
			'template': 'ENABLE_SVC_NOTIFICATIONS;service',
		},
		'DISABLE_SVC_NOTIFICATIONS': {
			'nagios_id': 23,
			'description': 'This command is used to prevent notifications from being sent out for the specified service.  You will have to re-enable notifications for this service before any alerts can be sent out in the future.',
			'brief': 'You are trying to disable notifications for a service',
			'template': 'DISABLE_SVC_NOTIFICATIONS;service',
		},
		'ENABLE_HOST_NOTIFICATIONS': {
			'nagios_id': 24,
			'description': 'This command is used to enable notifications for the specified host.  Notifications will only be sent out for the host state types you defined in your host definition.  Note that this command <i>does not</i> enable notifications for services associated with this host.',
			'brief': 'You are trying to enable notifications for a host',
			'template': 'ENABLE_HOST_NOTIFICATIONS;host_name',
		},
		'DISABLE_HOST_NOTIFICATIONS': {
			'nagios_id': 25,
			'description': 'This command is used to prevent notifications from being sent out for the specified host.  You will have to re-enable notifications for this host before any alerts can be sent out in the future.  Note that this command <i>does not</i> disable notifications for services associated with this host.',
			'brief': 'You are trying to disable notifications for a host',
			'template': 'DISABLE_HOST_NOTIFICATIONS;host_name',
		},
		'ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST': {
			'nagios_id': 26,
			'description': 'This command is used to enable notifications for all hosts and services that lie "beyond" the specified host (from the view of the monitoring process).',
			'brief': 'You are trying to enable notifications for all hosts and services beyond a host',
			'template': 'ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST;host_name',
		},
		'DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST': {
			'nagios_id': 27,
			'description': 'This command is used to temporarily prevent notifications from being sent out for all hosts and services that lie "beyone" the specified host (from the view of the monitoring process).',
			'brief': 'You are trying to disable notifications for all hosts and services beyond a host',
			'template': 'DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST;host_name',
		},
		'ENABLE_HOST_SVC_NOTIFICATIONS': {
			'nagios_id': 28,
			'description': 'This command is used to enable notifications for all services on the specified host.  Notifications will only be sent out for the service state types you defined in your service definition.  This <i>does not</i> enable notifications for the host unless you check the \'Enable for host too\' option.',
			'brief': 'You are trying to enable notifications for all services on a host',
			'template': 'ENABLE_HOST_SVC_NOTIFICATIONS;host_name',
		},
		'DISABLE_HOST_SVC_NOTIFICATIONS': {
			'nagios_id': 29,
			'description': 'This command is used to prevent notifications from being sent out for all services on the specified host.  You will have to re-enable notifications for all services associated with this host before any alerts can be sent out in the future.  This <i>does not</i> prevent notifications from being sent out about the host unless you check the \'Disable for host too\' option.',
			'brief': 'You are trying to disable notifications for all services on a host',
			'template': 'DISABLE_HOST_SVC_NOTIFICATIONS;host_name',
		},
		'PROCESS_SERVICE_CHECK_RESULT': {
			'nagios_id': 30,
			'description': 'This command is used to submit a passive check result for a service.  It is particularly useful for resetting security-related services to OK states once they have been dealt with.',
			'brief': 'You are trying to submit a passive check result for a service',
			'template': 'PROCESS_SERVICE_CHECK_RESULT;service;return_code;plugin_output',
		},
		'SAVE_STATE_INFORMATION': {
			'nagios_id': 31,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'SAVE_STATE_INFORMATION',
		},
		'READ_STATE_INFORMATION': {
			'nagios_id': 32,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'READ_STATE_INFORMATION',
		},
		'ACKNOWLEDGE_HOST_PROBLEM': {
			'nagios_id': 33,
			'description': 'This command is used to acknowledge a host problem.  When a host problem is acknowledged, future notifications about problems are temporarily disabled until the host changes from its current state. If you want acknowledgement to disable notifications until the host recovers, check the \'Sticky Acknowledgement\' checkbox. Contacts for this host will receive a notification about the acknowledgement, so they are aware that someone is working on the problem.  Additionally, a comment will also be added to the host. Make sure to enter your name and fill in a brief description of what you are doing in the comment field.  If you would like the host comment to remain once the acknowledgement is removed, check the \'Persistent Comment\' checkbox.  If you do not want an acknowledgement notification sent out to the appropriate contacts, uncheck the \'Send Notification\' checkbox.',
			'brief': 'You are trying to acknowledge a host problem',
			'template': 'ACKNOWLEDGE_HOST_PROBLEM;host_name;sticky;notify;persistent;author;comment',
		},
		'ACKNOWLEDGE_SVC_PROBLEM': {
			'nagios_id': 34,
			'description': 'This command is used to acknowledge a service problem.  When a service problem is acknowledged, future notifications about problems are temporarily disabled until the service changes from its current state. If you want acknowledgement to disable notifications until the service recovers, check the \'Sticky Acknowledgement\' checkbox. Contacts for this service will receive a notification about the acknowledgement, so they are aware that someone is working on the problem.  Additionally, a comment will also be added to the service. Make sure to enter your name and fill in a brief description of what you are doing in the comment field.  If you would like the service comment to remain once the acknowledgement is removed, check the \'Persistent Comment\' checkbox.  If you do not want an acknowledgement notification sent out to the appropriate contacts, uncheck the \'Send Notification\' checkbox.',
			'brief': 'You are trying to acknowledge a service problem',
			'template': 'ACKNOWLEDGE_SVC_PROBLEM;service;sticky;notify;persistent;author;comment',
		},
		'START_EXECUTING_SVC_CHECKS': {
			'nagios_id': 35,
			'description': 'This command is used to resume execution of active service checks on a program-wide basis.  Individual services which are disabled will still not be checked.',
			'brief': 'You are trying to start executing active service checks',
			'template': 'START_EXECUTING_SVC_CHECKS',
		},
		'STOP_EXECUTING_SVC_CHECKS': {
			'nagios_id': 36,
			'description': 'This command is used to temporarily stop service checks from being executed.  This will have the side effect of preventing any notifications from being sent out (for any and all services and hosts). Service checks will not be executed again until you issue a command to resume service check execution.',
			'brief': 'You are trying to stop executing active service checks',
			'template': 'STOP_EXECUTING_SVC_CHECKS',
		},
		'START_ACCEPTING_PASSIVE_SVC_CHECKS': {
			'nagios_id': 37,
			'description': 'This command is used to make the monitoring process start accepting passive service check results that it finds in the external command file.',
			'brief': 'You are trying to start accepting passive service checks',
			'template': 'START_ACCEPTING_PASSIVE_SVC_CHECKS',
		},
		'STOP_ACCEPTING_PASSIVE_SVC_CHECKS': {
			'nagios_id': 38,
			'description': 'This command is use to make the monitoring process stop accepting passive service check results that it finds in the external command file.  All passive check results that are found will be ignored.',
			'brief': 'You are trying to stop accepting passive service checks',
			'template': 'STOP_ACCEPTING_PASSIVE_SVC_CHECKS',
		},
		'ENABLE_PASSIVE_SVC_CHECKS': {
			'nagios_id': 39,
			'description': 'This command is used to allow the monitoring process to accept passive service check results that it finds in the external command file for this particular service.',
			'brief': 'You are trying to start accepting passive service checks for a service',
			'template': 'ENABLE_PASSIVE_SVC_CHECKS;service',
		},
		'DISABLE_PASSIVE_SVC_CHECKS': {
			'nagios_id': 40,
			'description': 'This command is used to stop the monitoring process accepting passive service check results that it finds in the external command file for this particular service.  All passive check results that are found for this service will be ignored.',
			'brief': 'You are trying to stop accepting passive service checks for a service',
			'template': 'DISABLE_PASSIVE_SVC_CHECKS;service',
		},
		'ENABLE_EVENT_HANDLERS': {
			'nagios_id': 41,
			'description': 'This command is used to allow the monitoring process to run host and service event handlers.',
			'brief': 'You are trying to enable event handlers',
			'template': 'ENABLE_EVENT_HANDLERS',
		},
		'DISABLE_EVENT_HANDLERS': {
			'nagios_id': 42,
			'description': 'This command is used to temporarily prevent the monitoring process from running any host or service event handlers.',
			'brief': 'You are trying to disable event handlers',
			'template': 'DISABLE_EVENT_HANDLERS',
		},
		'ENABLE_HOST_EVENT_HANDLER': {
			'nagios_id': 43,
			'description': 'This command is used to allow the monitoring process to run the host event handler for a service when necessary (if one is defined).',
			'brief': 'You are trying to enable the event handler for a host',
			'template': 'ENABLE_HOST_EVENT_HANDLER;host_name',
		},
		'DISABLE_HOST_EVENT_HANDLER': {
			'nagios_id': 44,
			'description': 'This command is used to temporarily prevent the monitoring process from running the host event handler for a host.',
			'brief': 'You are trying to disable the event handler for a host',
			'template': 'DISABLE_HOST_EVENT_HANDLER;host_name',
		},
		'ENABLE_SVC_EVENT_HANDLER': {
			'nagios_id': 45,
			'description': 'This command is used to allow the monitoring process to run the service event handler for a service when necessary (if one is defined).',
			'brief': 'You are trying to enable the event handler for a service',
			'template': 'ENABLE_SVC_EVENT_HANDLER;service',
		},
		'DISABLE_SVC_EVENT_HANDLER': {
			'nagios_id': 46,
			'description': 'This command is used to temporarily prevent the monitoring process from running the service event handler for a service.',
			'brief': 'You are trying to disable the event handler for a service',
			'template': 'DISABLE_SVC_EVENT_HANDLER;service',
		},
		'ENABLE_HOST_CHECK': {
			'nagios_id': 47,
			'description': 'This command is used to enable active checks of this host.',
			'brief': 'You are trying to enable active checks of a host',
			'template': 'ENABLE_HOST_CHECK;host_name',
		},
		'DISABLE_HOST_CHECK': {
			'nagios_id': 48,
			'description': 'This command is used to temporarily prevent the monitoring process from actively checking the status of a host.  If the monitoring process needs to check the status of this host, it will assume that it is in the same state that it was in before checks were disabled.',
			'brief': 'You are trying to disable active checks of a host',
			'template': 'DISABLE_HOST_CHECK;host_name',
		},
		'START_OBSESSING_OVER_SVC_CHECKS': {
			'nagios_id': 49,
			'description': 'This command is used to have the monitoring process start obsessing over service checks.  Read the documentation on distributed monitoring for more information on this.',
			'brief': 'You are trying to start obsessing over service checks',
			'template': 'START_OBSESSING_OVER_SVC_CHECKS',
		},
		'STOP_OBSESSING_OVER_SVC_CHECKS': {
			'nagios_id': 50,
			'description': 'This command is used stop the monitoring process from obsessing over service checks.',
			'brief': 'You are trying to stop obsessing over service checks',
			'template': 'STOP_OBSESSING_OVER_SVC_CHECKS',
		},
		'REMOVE_HOST_ACKNOWLEDGEMENT': {
			'nagios_id': 51,
			'description': 'This command is used to remove an acknowledgement for a host problem.  Once the acknowledgement is removed, notifications may start being sent out about the host problem. ',
			'brief': 'You are trying to remove a host acknowledgement',
			'template': 'REMOVE_HOST_ACKNOWLEDGEMENT;host_name',
		},
		'REMOVE_SVC_ACKNOWLEDGEMENT': {
			'nagios_id': 52,
			'description': 'This command is used to remove an acknowledgement for a service problem.  Once the acknowledgement is removed, notifications may start being sent out about the service problem.',
			'brief': 'You are trying to remove a service acknowledgement',
			'template': 'REMOVE_SVC_ACKNOWLEDGEMENT;service',
		},
		'SCHEDULE_FORCED_HOST_SVC_CHECKS': {
			'nagios_id': 53,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'SCHEDULE_FORCED_HOST_SVC_CHECKS;host_name;check_time',
		},
		'SCHEDULE_FORCED_SVC_CHECK': {
			'nagios_id': 54,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'SCHEDULE_FORCED_SVC_CHECK;service;check_time',
		},
		'SCHEDULE_HOST_DOWNTIME': {
			'nagios_id': 55,
			'description': 'This command is used to schedule downtime for a host. During the specified downtime, the monitoring process will not send notifications out about the host. When the scheduled downtime expires, the monitoring process will send out notifications for this host as it normally would.  Scheduled downtimes are preserved across program shutdowns and restarts.  Both the start and end times should be specified in the following format:  <b>mm/dd/yyyy hh:mm:ss</b>. If you select the <i>fixed</i> option, the downtime will be in effect between the start and end times you specify.  If you do not select the <i>fixed</i> option, the monitoring process will treat this as "flexible" downtime.  Flexible downtime starts when the host goes down or becomes unreachable (sometime between the start and end times you specified) and lasts as long as the duration of time you enter.  The duration fields do not apply for fixed downtime.',
			'brief': 'You are trying to schedule downtime for a host',
			'template': 'SCHEDULE_HOST_DOWNTIME;host_name;start_time;end_time;fixed;trigger_id;duration;author;comment',
		},
		'SCHEDULE_SVC_DOWNTIME': {
			'nagios_id': 56,
			'description': 'This command is used to schedule downtime for a service.  During the specified downtime, the monitoring process will not send notifications out about the service. When the scheduled downtime expires, the monitoring process will send out notifications for this service as it normally would.  Scheduled downtimes are preserved across program shutdowns and restarts.  Both the start and end times should be specified in the following format:  <b>mm/dd/yyyy hh:mm:ss</b>. option, the monitoring process will treat this as "flexible" downtime.  Flexible downtime starts when the service enters a non-OK state (sometime between the start and end times you specified) and lasts as long as the duration of time you enter.  The duration fields do not apply for fixed downtime.',
			'brief': 'You are trying to schedule downtime for a service',
			'template': 'SCHEDULE_SVC_DOWNTIME;service;start_time;end_time;fixed;trigger_id;duration;author;comment',
		},
		'ENABLE_HOST_FLAP_DETECTION': {
			'nagios_id': 57,
			'description': 'This command is used to enable flap detection for a specific host.  If flap detection is disabled on a program-wide basis, this will have no effect,',
			'brief': 'You are trying to enable flap detection for a host',
			'template': 'ENABLE_HOST_FLAP_DETECTION;host_name',
		},
		'DISABLE_HOST_FLAP_DETECTION': {
			'nagios_id': 58,
			'description': 'This command is used to disable flap detection for a specific host.',
			'brief': 'You are trying to disable flap detection for a host',
			'template': 'DISABLE_HOST_FLAP_DETECTION;host_name',
		},
		'ENABLE_SVC_FLAP_DETECTION': {
			'nagios_id': 59,
			'description': 'This command is used to enable flap detection for a specific service.  If flap detection is disabled on a program-wide basis, this will have no effect,',
			'brief': 'You are trying to enable flap detection for a service',
			'template': 'ENABLE_SVC_FLAP_DETECTION;service',
		},
		'DISABLE_SVC_FLAP_DETECTION': {
			'nagios_id': 60,
			'description': 'This command is used to disable flap detection for a specific service.',
			'brief': 'You are trying to disable flap detection for a service',
			'template': 'DISABLE_SVC_FLAP_DETECTION;service',
		},
		'ENABLE_FLAP_DETECTION': {
			'nagios_id': 61,
			'description': 'This command is used to enable flap detection for hosts and services on a program-wide basis.  Individual hosts and services may have flap detection disabled.',
			'brief': 'You are trying to enable flap detection for hosts and services',
			'template': 'ENABLE_FLAP_DETECTION',
		},
		'DISABLE_FLAP_DETECTION': {
			'nagios_id': 62,
			'description': 'This command is used to disable flap detection for hosts and services on a program-wide basis.',
			'brief': 'You are trying to disable flap detection for hosts and services',
			'template': 'DISABLE_FLAP_DETECTION',
		},
		'ENABLE_HOSTGROUP_SVC_NOTIFICATIONS': {
			'nagios_id': 63,
			'description': 'This command is used to enable notifications for all services in the specified hostgroup.  Notifications will only be sent out for the service state types you defined in your service definitions.  This <i>does not</i> enable notifications for the hosts in this hostgroup unless you check the \'Enable for hosts too\' option.',
			'brief': 'You are trying to enable notifications for all services in a hostgroup',
			'template': 'ENABLE_HOSTGROUP_SVC_NOTIFICATIONS;hostgroup_name',
		},
		'DISABLE_HOSTGROUP_SVC_NOTIFICATIONS': {
			'nagios_id': 64,
			'description': 'This command is used to prevent notifications from being sent out for all services in the specified hostgroup.  You will have to re-enable notifications for all services in this hostgroup before any alerts can be sent out in the future.  This <i>does not</i> prevent notifications from being sent out about the hosts in this hostgroup unless you check the \'Disable for hosts too\' option.',
			'brief': 'You are trying to disable notifications for all services in a hostgroup',
			'template': 'DISABLE_HOSTGROUP_SVC_NOTIFICATIONS;hostgroup_name',
		},
		'ENABLE_HOSTGROUP_HOST_NOTIFICATIONS': {
			'nagios_id': 65,
			'description': 'This command is used to enable notifications for all hosts in the specified hostgroup.  Notifications will only be sent out for the host state types you defined in your host definitions.',
			'brief': 'You are trying to enable notifications for all hosts in a hostgroup',
			'template': 'ENABLE_HOSTGROUP_HOST_NOTIFICATIONS;hostgroup_name',
		},
		'DISABLE_HOSTGROUP_HOST_NOTIFICATIONS': {
			'nagios_id': 66,
			'description': 'This command is used to prevent notifications from being sent out for all hosts in the specified hostgroup.  You will have to re-enable notifications for all hosts in this hostgroup before any alerts can be sent out in the future.',
			'brief': 'You are trying to disable notifications for all hosts in a hostgroup',
			'template': 'DISABLE_HOSTGROUP_HOST_NOTIFICATIONS;hostgroup_name',
		},
		'ENABLE_HOSTGROUP_SVC_CHECKS': {
			'nagios_id': 67,
			'description': 'This command is used to enable active checks of all services in the specified hostgroup.  This <i>does not</i> enable active checks of the hosts in the hostgroup unless you check the \'Enable for hosts too\' option.',
			'brief': 'You are trying to enable active checks of all services in a hostgroup',
			'template': 'ENABLE_HOSTGROUP_SVC_CHECKS;hostgroup_name',
		},
		'DISABLE_HOSTGROUP_SVC_CHECKS': {
			'nagios_id': 68,
			'description': 'This command is used to disable active checks of all services in the specified hostgroup.  This <i>does not</i> disable checks of the hosts in the hostgroup unless you check the \'Disable for hosts too\' option.',
			'brief': 'You are trying to disable active checks of all services in a hostgroup',
			'template': 'DISABLE_HOSTGROUP_SVC_CHECKS;hostgroup_name',
		},
		'DEL_HOST_DOWNTIME': {
			'nagios_id': 78,
			'description': 'This command is used to cancel active or pending scheduled downtime for the specified host.',
			'brief': 'You are trying to cancel scheduled downtime for a host',
			'template': 'DEL_HOST_DOWNTIME;downtime_id',
		},
		'DEL_SVC_DOWNTIME': {
			'nagios_id': 79,
			'description': 'This command is used to cancel active or pending scheduled downtime for the specified service.',
			'brief': 'You are trying to cancel scheduled downtime for a service',
			'template': 'DEL_SVC_DOWNTIME;downtime_id',
		},
		'ENABLE_FAILURE_PREDICTION': {
			'nagios_id': 80,
			'description': 'This command is used to enable failure prediction for hosts and services on a program-wide basis.  Individual hosts and services may have failure prediction disabled.',
			'brief': 'You are trying to enable failure prediction for hosts and service',
			'template': 'ENABLE_FAILURE_PREDICTION',
		},
		'DISABLE_FAILURE_PREDICTION': {
			'nagios_id': 81,
			'description': 'This command is used to disable failure prediction for hosts and services on a program-wide basis.',
			'brief': 'You are trying to disable failure prediction for hosts and service',
			'template': 'DISABLE_FAILURE_PREDICTION',
		},
		'ENABLE_PERFORMANCE_DATA': {
			'nagios_id': 82,
			'description': 'This command is used to enable the processing of performance data for hosts and services on a program-wide basis.  Individual hosts and services may have performance data processing disabled.',
			'brief': 'You are trying to enable performance data processing for hosts and services',
			'template': 'ENABLE_PERFORMANCE_DATA',
		},
		'DISABLE_PERFORMANCE_DATA': {
			'nagios_id': 83,
			'description': 'This command is used to disable the processing of performance data for hosts and services on a program-wide basis.',
			'brief': 'You are trying to disable performance data processing for hosts and services',
			'template': 'DISABLE_PERFORMANCE_DATA',
		},
		'SCHEDULE_HOSTGROUP_HOST_DOWNTIME': {
			'nagios_id': 84,
			'description': 'This command is used to schedule downtime for all hosts in a hostgroup.  During the specified downtime, the monitoring process will not send notifications out about the hosts. When the scheduled downtime expires, the monitoring process will send out notifications for the hosts as it normally would.  Scheduled downtimes are preserved across program shutdowns and restarts.  Both the start and end times should be specified in the following format:  <b>mm/dd/yyyy hh:mm:ss</b>. If you select the <i>fixed</i> option, the downtime will be in effect between the start and end times you specify.  If you do not select the <i>fixed</i> option, the monitoring process will treat this as "flexible" downtime.  Flexible downtime starts when a host goes down or becomes unreachable (sometime between the start and end times you specified) and lasts as long as the duration of time you enter.  The duration fields do not apply for fixed dowtime.',
			'brief': 'You are trying to schedule downtime for all hosts in a hostgroup',
			'template': 'SCHEDULE_HOSTGROUP_HOST_DOWNTIME;hostgroup_name;start_time;end_time;fixed;trigger_id;duration;author;comment',
		},
		'SCHEDULE_HOSTGROUP_SVC_DOWNTIME': {
			'nagios_id': 85,
			'description': 'This command is used to schedule downtime for all services in a hostgroup.  During the specified downtime, the monitoring process will not send notifications out about the services. When the scheduled downtime expires, the monitoring process will send out notifications for the services as it normally would.  Scheduled downtimes are preserved across program shutdowns and restarts.  Both the start and end times should be specified in the following format:  <b>mm/dd/yyyy hh:mm:ss</b>. If you select the <i>fixed</i> option, the downtime will be in effect between the start and end times you specify.  If you do not select the <i>fixed</i> option, the monitoring process will treat this as "flexible" downtime.  Flexible downtime starts when a service enters a non-OK state (sometime between the start and end times you specified) and lasts as long as the duration of time you enter.  The duration fields do not apply for fixed dowtime. Note that scheduling downtime for services does not automatically schedule downtime for the hosts those services are associated with.  If you want to also schedule downtime for all hosts in the hostgroup, check the \'Schedule downtime for hosts too\' option.',
			'brief': 'You are trying to schedule downtime for all services in a hostgroup',
			'template': 'SCHEDULE_HOSTGROUP_SVC_DOWNTIME;hostgroup_name;start_time;end_time;fixed;trigger_id;duration;author;comment',
		},
		'SCHEDULE_HOST_SVC_DOWNTIME': {
			'nagios_id': 86,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'SCHEDULE_HOST_SVC_DOWNTIME;host_name;start_time;end_time;fixed;trigger_id;duration;author;comment',
		},
		'PROCESS_HOST_CHECK_RESULT': {
			'nagios_id': 87,
			'description': 'This command is used to submit a passive check result for a host.',
			'brief': 'You are trying to submit a passive check result for a host',
			'template': 'PROCESS_HOST_CHECK_RESULT;host_name;status_code;plugin_output',
		},
		'START_EXECUTING_HOST_CHECKS': {
			'nagios_id': 88,
			'description': 'This command is used to enable active host checks on a program-wide basis.',
			'brief': 'You are trying to start executing host checks',
			'template': 'START_EXECUTING_HOST_CHECKS',
		},
		'STOP_EXECUTING_HOST_CHECKS': {
			'nagios_id': 89,
			'description': 'This command is used to disable active host checks on a program-wide basis.',
			'brief': 'You are trying to stop executing host checks',
			'template': 'STOP_EXECUTING_HOST_CHECKS',
		},
		'START_ACCEPTING_PASSIVE_HOST_CHECKS': {
			'nagios_id': 90,
			'description': 'This command is used to have the monitoring process start obsessing over host checks.  Read the documentation on distributed monitoring for more information on this.',
			'brief': 'You are trying to start accepting passive host checks',
			'template': 'START_ACCEPTING_PASSIVE_HOST_CHECKS',
		},
		'STOP_ACCEPTING_PASSIVE_HOST_CHECKS': {
			'nagios_id': 91,
			'description': 'This command is used to stop the monitoring process from obsessing over host checks.',
			'brief': 'You are trying to stop accepting passive host checks',
			'template': 'STOP_ACCEPTING_PASSIVE_HOST_CHECKS',
		},
		'ENABLE_PASSIVE_HOST_CHECKS': {
			'nagios_id': 92,
			'description': 'This command is used to allow the monitoring process to accept passive host check results that it finds in the external command file for a host.',
			'brief': 'You are trying to start accepting passive checks for a host',
			'template': 'ENABLE_PASSIVE_HOST_CHECKS;host_name',
		},
		'DISABLE_PASSIVE_HOST_CHECKS': {
			'nagios_id': 93,
			'description': 'This command is used to stop the monitoring process from accepting passive host check results that it finds in the external command file for a host.  All passive check results that are found for this host will be ignored.',
			'brief': 'You are trying to stop accepting passive checks for a host',
			'template': 'DISABLE_PASSIVE_HOST_CHECKS;host_name',
		},
		'START_OBSESSING_OVER_HOST_CHECKS': {
			'nagios_id': 94,
			'description': 'This command is used to have the monitoring process start obsessing over host checks.  Read the documentation on distributed monitoring for more information on this.',
			'brief': 'You are trying to start obsessing over host checks',
			'template': 'START_OBSESSING_OVER_HOST_CHECKS',
		},
		'STOP_OBSESSING_OVER_HOST_CHECKS': {
			'nagios_id': 95,
			'description': 'This command is used to stop the monitoring process from obsessing over host checks.',
			'brief': 'You are trying to stop obsessing over host checks',
			'template': 'STOP_OBSESSING_OVER_HOST_CHECKS',
		},
		'SCHEDULE_HOST_CHECK': {
			'nagios_id': 96,
			'description': 'This command is used to schedule the next check of a host. the monitoring process will re-queue the host to be checked at the time you specify. If you select the <i>force check</i> option, the monitoring process will force a check of the host regardless of both what time the scheduled check occurs and whether or not checks are enabled for the host.',
			'brief': 'You are trying to schedule a host check',
			'template': 'SCHEDULE_HOST_CHECK;host_name;check_time',
		},
		'SCHEDULE_FORCED_HOST_CHECK': {
			'nagios_id': 98,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'SCHEDULE_FORCED_HOST_CHECK;host_name;check_time',
		},
		'START_OBSESSING_OVER_SVC': {
			'nagios_id': 99,
			'description': 'This command is used to have the monitoring process start obsessing over a service.',
			'brief': 'You are trying to start obsessing over a service',
			'template': 'START_OBSESSING_OVER_SVC;service',
		},
		'STOP_OBSESSING_OVER_SVC': {
			'nagios_id': 100,
			'description': 'This command is used to stop the monitoring process from obsessing over a service.',
			'brief': 'You are trying to stop obsessing over a service',
			'template': 'STOP_OBSESSING_OVER_SVC;service',
		},
		'START_OBSESSING_OVER_HOST': {
			'nagios_id': 101,
			'description': 'This command is used to have the monitoring process start obsessing over a host.',
			'brief': 'You are trying to start obsessing over a host',
			'template': 'START_OBSESSING_OVER_HOST;host_name',
		},
		'STOP_OBSESSING_OVER_HOST': {
			'nagios_id': 102,
			'description': 'This command is used to stop the monitoring process from obsessing over a host.',
			'brief': 'You are trying to stop obsessing over a host',
			'template': 'STOP_OBSESSING_OVER_HOST;host_name',
		},
		'ENABLE_HOSTGROUP_HOST_CHECKS': {
			'nagios_id': 103,
			'description': 'This command is used to enable active checks of all hosts in the specified hostgroup. This <i>does not</i> enable active checks of the services in the hostgroup.',
			'brief': 'You are trying to enable active checks of all hosts in a hostgroup',
			'template': 'ENABLE_HOSTGROUP_HOST_CHECKS;hostgroup_name',
		},
		'DISABLE_HOSTGROUP_HOST_CHECKS': {
			'nagios_id': 104,
			'description': 'This command is used to disable active checks of all hosts in the specified hostgroup. This <i>does not</i> disable  active checks of the services in the hostgroup.',
			'brief': 'You are trying to  enable active checks of all hosts in a hostgroup',
			'template': 'DISABLE_HOSTGROUP_HOST_CHECKS;hostgroup_name',
		},
		'ENABLE_HOSTGROUP_PASSIVE_SVC_CHECKS': {
			'nagios_id': 105,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_HOSTGROUP_PASSIVE_SVC_CHECKS;hostgroup_name',
		},
		'DISABLE_HOSTGROUP_PASSIVE_SVC_CHECKS': {
			'nagios_id': 106,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_HOSTGROUP_PASSIVE_SVC_CHECKS;hostgroup_name',
		},
		'ENABLE_HOSTGROUP_PASSIVE_HOST_CHECKS': {
			'nagios_id': 107,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_HOSTGROUP_PASSIVE_HOST_CHECKS;hostgroup_name',
		},
		'DISABLE_HOSTGROUP_PASSIVE_HOST_CHECKS': {
			'nagios_id': 108,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_HOSTGROUP_PASSIVE_HOST_CHECKS;hostgroup_name',
		},
		'ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS': {
			'nagios_id': 109,
			'description': 'This command is used to enable notifications for all services in the specified servicegroup.  Notifications will only be sent out for the service state types you defined in your service definitions.  This <i>does not</i> enable notifications for the hosts in this servicegroup unless you check the \'Enable for hosts too\' option.',
			'brief': 'You are trying to enable notifications for all services in a servicegroup',
			'template': 'ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS;servicegroup_name',
		},
		'DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS': {
			'nagios_id': 110,
			'description': 'This command is used to prevent notifications from being sent out for all services in the specified servicegroup.  You will have to re-enable notifications for all services in this servicegroup before any alerts can be sent out in the future.  This <i>does not</i> prevent notifications from being sent out about the hosts in this servicegroup unless you check the \'Disable for hosts too\' option.',
			'brief': 'You are trying to disable notifications for all services in a servicegroup',
			'template': 'DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS;servicegroup_name',
		},
		'ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS': {
			'nagios_id': 111,
			'description': 'This command is used to enable notifications for all hosts in the specified servicegroup.  Notifications will only be sent out for the host state types you defined in your host definitions.',
			'brief': 'You are trying to enable notifications for all hosts in a servicegroup',
			'template': 'ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS;servicegroup_name',
		},
		'DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS': {
			'nagios_id': 112,
			'description': 'This command is used to prevent notifications from being sent out for all hosts in the specified servicegroup.  You will have to re-enable notifications for all hosts in this servicegroup before any alerts can be sent out in the future.',
			'brief': 'You are trying to disable notifications for all hosts in a servicegroup',
			'template': 'DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS;servicegroup_name',
		},
		'ENABLE_SERVICEGROUP_SVC_CHECKS': {
			'nagios_id': 113,
			'description': 'This command is used to enable active checks of all services in the specified servicegroup.  This <i>does not</i> enable active checks of the hosts in the servicegroup unless you check the \'Enable for hosts too\' option.',
			'brief': 'You are trying to enable active checks of all services in a servicegroup',
			'template': 'ENABLE_SERVICEGROUP_SVC_CHECKS;servicegroup_name',
		},
		'DISABLE_SERVICEGROUP_SVC_CHECKS': {
			'nagios_id': 114,
			'description': 'This command is used to disable active checks of all services in the specified servicegroup.  This <i>does not</i> disable checks of the hosts in the servicegroup unless you check the \'Disable for hosts too\' option.',
			'brief': 'You are trying to disable active checks of all services in a servicegroup',
			'template': 'DISABLE_SERVICEGROUP_SVC_CHECKS;servicegroup_name',
		},
		'ENABLE_SERVICEGROUP_HOST_CHECKS': {
			'nagios_id': 115,
			'description': 'This command is used to enable active checks of all hosts in the specified servicegroup.  This <i>does not</i> enable active checks of the services in the servicegroup.',
			'brief': 'You are trying to enable active checks of all services in a servicegroup',
			'template': 'ENABLE_SERVICEGROUP_HOST_CHECKS;servicegroup_name',
		},
		'DISABLE_SERVICEGROUP_HOST_CHECKS': {
			'nagios_id': 116,
			'description': 'This command is used to disable active checks of all hosts in the specified servicegroup.  This <i>does not</i> disable checks of the services in the servicegroup.',
			'brief': 'You are trying to disable active checks of all hosts in a servicegroup',
			'template': 'DISABLE_SERVICEGROUP_HOST_CHECKS;servicegroup_name',
		},
		'ENABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS': {
			'nagios_id': 117,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS;servicegroup_name',
		},
		'DISABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS': {
			'nagios_id': 118,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS;servicegroup_name',
		},
		'ENABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS': {
			'nagios_id': 119,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS;servicegroup_name',
		},
		'DISABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS': {
			'nagios_id': 120,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS;servicegroup_name',
		},
		'SCHEDULE_SERVICEGROUP_HOST_DOWNTIME': {
			'nagios_id': 121,
			'description': 'This command is used to schedule downtime for all hosts in a servicegroup.  During the specified downtime, the monitoring process will not send notifications out about the hosts. When the scheduled downtime expires, the monitoring process will send out notifications for the hosts as it normally would.  Scheduled downtimes are preserved across program shutdowns and restarts.  Both the start and end times should be specified in the following format:  <b>mm/dd/yyyy hh:mm:ss</b>. If you select the <i>fixed</i> option, the downtime will be in effect between the start and end times you specify.  If you do not select the <i>fixed</i> option, the monitoring process will treat this as "flexible" downtime.  Flexible downtime starts when a host goes down or becomes unreachable (sometime between the start and end times you specified) and lasts as long as the duration of time you enter.  The duration fields do not apply for fixed dowtime.',
			'brief': 'You are trying to schedule downtime for all hosts in a servicegroup',
			'template': 'SCHEDULE_SERVICEGROUP_HOST_DOWNTIME;servicegroup_name;start_time;end_time;fixed;trigger_id;duration;author;comment',
		},
		'SCHEDULE_SERVICEGROUP_SVC_DOWNTIME': {
			'nagios_id': 122,
			'description': 'This command is used to schedule downtime for all services in a servicegroup.  During the specified downtime, the monitoring process will not send notifications out about the services. When the scheduled downtime expires, the monitoring process will send out notifications for the services as it normally would.  Scheduled downtimes are preserved across program shutdowns and restarts.  Both the start and end times should be specified in the following format:  <b>mm/dd/yyyy hh:mm:ss</b>. If you select the <i>fixed</i> option, the downtime will be in effect between the start and end times you specify.  If you do not select the <i>fixed</i> option, the monitoring process will treat this as "flexible" downtime.  Flexible downtime starts when a service enters a non-OK state (sometime between the start and end times you specified) and lasts as long as the duration of time you enter.  The duration fields do not apply for fixed dowtime. Note that scheduling downtime for services does not automatically schedule downtime for the hosts those services are associated with.  If you want to also schedule downtime for all hosts in the servicegroup, check the \'Schedule downtime for hosts too\' option.',
			'brief': 'You are trying to schedule downtime for all services in a servicegroup',
			'template': 'SCHEDULE_SERVICEGROUP_SVC_DOWNTIME;servicegroup_name;start_time;end_time;fixed;trigger_id;duration;author;comment',
		},
		'CHANGE_GLOBAL_HOST_EVENT_HANDLER': {
			'nagios_id': 123,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_GLOBAL_HOST_EVENT_HANDLER;event_handler_command',
		},
		'CHANGE_GLOBAL_SVC_EVENT_HANDLER': {
			'nagios_id': 124,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_GLOBAL_SVC_EVENT_HANDLER;event_handler_command',
		},
		'CHANGE_HOST_EVENT_HANDLER': {
			'nagios_id': 125,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_HOST_EVENT_HANDLER;host_name;event_handler_command',
		},
		'CHANGE_SVC_EVENT_HANDLER': {
			'nagios_id': 126,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_SVC_EVENT_HANDLER;service;event_handler_command',
		},
		'CHANGE_HOST_CHECK_COMMAND': {
			'nagios_id': 127,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_HOST_CHECK_COMMAND;host_name;check_command',
		},
		'CHANGE_SVC_CHECK_COMMAND': {
			'nagios_id': 128,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_SVC_CHECK_COMMAND;service;check_command',
		},
		'CHANGE_NORMAL_HOST_CHECK_INTERVAL': {
			'nagios_id': 129,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_NORMAL_HOST_CHECK_INTERVAL;host_name;check_interval',
		},
		'CHANGE_NORMAL_SVC_CHECK_INTERVAL': {
			'nagios_id': 130,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_NORMAL_SVC_CHECK_INTERVAL;service;check_interval',
		},
		'CHANGE_RETRY_SVC_CHECK_INTERVAL': {
			'nagios_id': 131,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_RETRY_SVC_CHECK_INTERVAL;service;check_interval',
		},
		'CHANGE_MAX_HOST_CHECK_ATTEMPTS': {
			'nagios_id': 132,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_MAX_HOST_CHECK_ATTEMPTS;host_name;check_attempts',
		},
		'CHANGE_MAX_SVC_CHECK_ATTEMPTS': {
			'nagios_id': 133,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_MAX_SVC_CHECK_ATTEMPTS;service;check_attempts',
		},
		'SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME': {
			'nagios_id': 134,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME;host_name;start_time;end_time;fixed;trigger_id;duration;author;comment',
		},
		'ENABLE_HOST_AND_CHILD_NOTIFICATIONS': {
			'nagios_id': 135,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_HOST_AND_CHILD_NOTIFICATIONS;host_name',
		},
		'DISABLE_HOST_AND_CHILD_NOTIFICATIONS': {
			'nagios_id': 136,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_HOST_AND_CHILD_NOTIFICATIONS;host_name',
		},
		'SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME': {
			'nagios_id': 137,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME;host_name;start_time;end_time;fixed;trigger_id;duration;author;comment',
		},
		'ENABLE_SERVICE_FRESHNESS_CHECKS': {
			'nagios_id': 138,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_SERVICE_FRESHNESS_CHECKS',
		},
		'DISABLE_SERVICE_FRESHNESS_CHECKS': {
			'nagios_id': 139,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_SERVICE_FRESHNESS_CHECKS',
		},
		'ENABLE_HOST_FRESHNESS_CHECKS': {
			'nagios_id': 140,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_HOST_FRESHNESS_CHECKS',
		},
		'DISABLE_HOST_FRESHNESS_CHECKS': {
			'nagios_id': 141,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_HOST_FRESHNESS_CHECKS',
		},
		'SET_HOST_NOTIFICATION_NUMBER': {
			'nagios_id': 142,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'SET_HOST_NOTIFICATION_NUMBER;host_name;notification_number',
		},
		'SET_SVC_NOTIFICATION_NUMBER': {
			'nagios_id': 143,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'SET_SVC_NOTIFICATION_NUMBER;service;notification_number',
		},
		'CHANGE_HOST_CHECK_TIMEPERIOD': {
			'nagios_id': 144,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_HOST_CHECK_TIMEPERIOD;host_name;timeperiod',
		},
		'CHANGE_SVC_CHECK_TIMEPERIOD': {
			'nagios_id': 145,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_SVC_CHECK_TIMEPERIOD;service;check_timeperiod',
		},
		'PROCESS_FILE': {
			'nagios_id': 146,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'PROCESS_FILE;file_name;delete',
		},
		'CHANGE_CUSTOM_HOST_VAR': {
			'nagios_id': 147,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_CUSTOM_HOST_VAR;host_name;varname;varvalue',
		},
		'CHANGE_CUSTOM_SVC_VAR': {
			'nagios_id': 148,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_CUSTOM_SVC_VAR;service;varname;varvalue',
		},
		'CHANGE_CUSTOM_CONTACT_VAR': {
			'nagios_id': 149,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_CUSTOM_CONTACT_VAR;contact_name;varname;varvalue',
		},
		'ENABLE_CONTACT_HOST_NOTIFICATIONS': {
			'nagios_id': 150,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_CONTACT_HOST_NOTIFICATIONS;contact_name',
		},
		'DISABLE_CONTACT_HOST_NOTIFICATIONS': {
			'nagios_id': 151,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_CONTACT_HOST_NOTIFICATIONS;contact_name',
		},
		'ENABLE_CONTACT_SVC_NOTIFICATIONS': {
			'nagios_id': 152,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_CONTACT_SVC_NOTIFICATIONS;contact_name',
		},
		'DISABLE_CONTACT_SVC_NOTIFICATIONS': {
			'nagios_id': 153,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_CONTACT_SVC_NOTIFICATIONS;contact_name',
		},
		'ENABLE_CONTACTGROUP_HOST_NOTIFICATIONS': {
			'nagios_id': 154,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_CONTACTGROUP_HOST_NOTIFICATIONS;contactgroup_name',
		},
		'DISABLE_CONTACTGROUP_HOST_NOTIFICATIONS': {
			'nagios_id': 155,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_CONTACTGROUP_HOST_NOTIFICATIONS;contactgroup_name',
		},
		'ENABLE_CONTACTGROUP_SVC_NOTIFICATIONS': {
			'nagios_id': 156,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'ENABLE_CONTACTGROUP_SVC_NOTIFICATIONS;contactgroup_name',
		},
		'DISABLE_CONTACTGROUP_SVC_NOTIFICATIONS': {
			'nagios_id': 157,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'DISABLE_CONTACTGROUP_SVC_NOTIFICATIONS;contactgroup_name',
		},
		'CHANGE_RETRY_HOST_CHECK_INTERVAL': {
			'nagios_id': 158,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_RETRY_HOST_CHECK_INTERVAL;service;check_interval',
		},
		'SEND_CUSTOM_HOST_NOTIFICATION': {
			'nagios_id': 159,
			'description': 'This command is used to send a custom notification about the specified host.  Useful in emergencies when you need to notify admins of an issue regarding a monitored system or service. Custom notifications normally follow the regular notification logic in the monitoring process.  Selecting the <i>Forced</i> option will force the notification to be sent out, regardless of the time restrictions, whether or not notifications are enabled, etc.  Selecting the <i>Broadcast</i> option causes the notification to be sent out to all normal (non-escalated) and escalated contacts.  These options allow you to override the normal notification logic if you need to get an important message out.',
			'brief': 'You are trying to send a custom host notification',
			'template': 'SEND_CUSTOM_HOST_NOTIFICATION;host_name;options;author;comment',
		},
		'SEND_CUSTOM_SVC_NOTIFICATION': {
			'nagios_id': 160,
			'description': 'This command is used to send a custom notification about the specified service.  Useful in emergencies when you need to notify admins of an issue regarding a monitored system or service. Custom notifications normally follow the regular notification logic in the monitoring process.  Selecting the <i>Forced</i> option will force the notification to be sent out, regardless of the time restrictions, whether or not notifications are enabled, etc.  Selecting the <i>Broadcast</i> option causes the notification to be sent out to all normal (non-escalated) and escalated contacts.  These options allow you to override the normal notification logic if you need to get an important message out.',
			'brief': 'You are trying to send a custom service notification',
			'template': 'SEND_CUSTOM_SVC_NOTIFICATION;service;options;author;comment',
		},
		'CHANGE_HOST_NOTIFICATION_TIMEPERIOD': {
			'nagios_id': 161,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_HOST_NOTIFICATION_TIMEPERIOD;host_name;notification_timeperiod',
		},
		'CHANGE_SVC_NOTIFICATION_TIMEPERIOD': {
			'nagios_id': 162,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_SVC_NOTIFICATION_TIMEPERIOD;service;notification_timeperiod',
		},
		'CHANGE_CONTACT_HOST_NOTIFICATION_TIMEPERIOD': {
			'nagios_id': 163,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_CONTACT_HOST_NOTIFICATION_TIMEPERIOD;contact_name;notification_timeperiod',
		},
		'CHANGE_CONTACT_SVC_NOTIFICATION_TIMEPERIOD': {
			'nagios_id': 164,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_CONTACT_SVC_NOTIFICATION_TIMEPERIOD;contact_name;notification_timeperiod',
		},
		'CHANGE_HOST_MODATTR': {
			'nagios_id': 165,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_HOST_MODATTR;host_name;value',
		},
		'CHANGE_SVC_MODATTR': {
			'nagios_id': 166,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_SVC_MODATTR;service;value',
		},
		'CHANGE_CONTACT_MODATTR': {
			'nagios_id': 167,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_CONTACT_MODATTR;contact_name;value',
		},
		'CHANGE_CONTACT_MODHATTR': {
			'nagios_id': 168,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_CONTACT_MODHATTR;contact_name;value',
		},
		'CHANGE_CONTACT_MODSATTR': {
			'nagios_id': 169,
			'description': 'This command is not implemented',
			'brief': 'You are trying to execute an unsupported command.',
			'template': 'CHANGE_CONTACT_MODSATTR;contact_name;value',
		},
	}

	def __init__(self, name=False):
		self.info = False
		self.use_command(name)
		self.params = False
		self.command_string = False
		self.pipe_fd = -1
		self.pipe_path = False


	def use_command(self, name=False):
		self.name = name
		if name == False:
			return False
		name = name.upper()
		self.info = self.command_info.get(name, False)
		if self.info == False:
			return False
		return True


	def set_pipe_path(self, pipe_path):
		self.pipe_path = pipe_path


	def set_params(self, params):
		if not self.info:
			return False
		template_ary = self.info['template'].split(';')
		# + 1 since the template_ary contains the command name

		if len(template_ary) != len(params) + 1:
			return False

		# commands without parameters are more or less done here
		if len(template_ary) == 1:
			self.command_string = self.name.upper()
			return True

		if type(params) == type([1, 1]):
			self.command_string = self.name.upper() + ';' + ';'.join(params)
			return True

		if type(params) != type({1: 0, 1: 0}):
			return False

		cmd_string = self.name.upper()
		for k in template_ary[1:]:
			v = params.get(k, False)
			if v == False:
				print("No value for parameter %s" % k)
				return False
			cmd_string = "%s;%s" % (cmd_string, v)
		self.command_string = cmd_string
		return True

	def open_pipe(self, reopen=False):
		if self.pipe_path == False:
			return False
		if self.pipe_fd >= 0 and reopen == False:
			return True

		if not os.access(self.pipe_path, os.W_OK):
			return False
		# pipes that aren't being read stall while we connect
		# to them, so we must add a stupid signal handler to
		# avoid hanging indefinitely in case Monitor isn't running.
		# Two seconds should be ample time for opening the pipe and
		# resetting the alarm before it goes off.
		signal.signal(signal.SIGALRM, _cmd_pipe_sighandler)
		signal.alarm(2)
		try:
			cmd_fd = os.open(self.pipe_path, posix.O_WRONLY)
		except OSError, ose:
			if ose.errno == errno.EAGAIN:
				return False
			return False

		# now we reset the timer
		signal.alarm(0)
		self.pipe_fd = cmd_fd
		return True


	def submit_raw(self, cmd):
		if not cmd.startswith('['):
			cmd = "[%d] %s" % (time.time(), cmd)
		if not cmd.endswith('\n'):
			cmd = "%s\n" % cmd

		if self.open_pipe() == False:
			return False

		if os.write(self.pipe_fd, cmd) == len(cmd):
			return True

		return False


	def submit(self, name=False, params=False):
		if name != False:
			self.use_command(name)

		if params != False:
			self.set_params(params)

		if not self.command_string:
			return False

		return self.submit_raw(self.command_string)
