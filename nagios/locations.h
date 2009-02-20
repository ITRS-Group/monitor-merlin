/************************************************************************
 *
 * Nagios Locations Header File
 * Written By: Ethan Galstad (egalstad@nagios.org)
 * Last Modified: 04-30-2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************/

#define DEFAULT_TEMP_FILE			"/opt/monitor/var/tempfile"
#define DEFAULT_TEMP_PATH                       "/tmp"
#define DEFAULT_CHECK_RESULT_PATH		"/opt/monitor/var/spool/checkresults"
#define DEFAULT_STATUS_FILE			"/opt/monitor/var/status.dat"
#define DEFAULT_LOG_FILE			"/opt/monitor/var/nagios.log"
#define DEFAULT_LOG_ARCHIVE_PATH		"/opt/monitor/var/archives/"
#define DEFAULT_DEBUG_FILE                      "/opt/monitor/var/nagios.debug"
#define DEFAULT_COMMENT_FILE			"/opt/monitor/var/comments.dat"
#define DEFAULT_DOWNTIME_FILE			"/opt/monitor/var/downtime.dat"
#define DEFAULT_RETENTION_FILE			"/opt/monitor/var/retention.dat"
#define DEFAULT_COMMAND_FILE			"/opt/monitor/var/rw/nagios.cmd"
#define DEFAULT_CONFIG_FILE			"/opt/monitor/etc/nagios.cfg"
#define DEFAULT_PHYSICAL_HTML_PATH		"/opt/monitor/share"
#define DEFAULT_URL_HTML_PATH			"/nagios"
#define DEFAULT_PHYSICAL_CGIBIN_PATH		"/opt/monitor/sbin"
#define DEFAULT_URL_CGIBIN_PATH			"/nagios/cgi-bin"
#define DEFAULT_CGI_CONFIG_FILE			"/opt/monitor/etc/cgi.cfg"
#define DEFAULT_LOCK_FILE			"/opt/monitor/var/nagios.lock"
#define DEFAULT_OBJECT_CACHE_FILE		"/opt/monitor/var/objects.cache"
#define DEFAULT_PRECACHED_OBJECT_FILE		"/opt/monitor/var/objects.precache"
#define DEFAULT_EVENT_BROKER_FILE		"/opt/monitor/var/broker.socket"
#define DEFAULT_P1_FILE				"/opt/monitor/bin/p1.pl"	/**** EMBEDDED PERL ****/
#define DEFAULT_AUTH_FILE			""			/**** EMBEDDED PERL - IS THIS USED? ****/
