/* HouseDepot - a log and ressource file storage service.
 *
 * Copyright 2022, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 * ------------------------------------------------------------------------
 *
 * housedepot_archive.c: a module to manage archive backup and restore.
 *
 * const char *housedepot_archive_backup (void);
 *
 *    Create a new backup archive, if necessary (i.e. if the state of
 *    the repositories has changed since the most recent backup).
 *
 * void housedepot_archive_initialize (const char *hostname,
 *                                     const char *root, int state);
 *
 *    Set the host name and initialize the module's resources.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "echttp.h"
#include "echttp_libc.h"

#include "housestate.h"
#include "housedepot_archive.h"

#define DEBUG if (housedepot_isdebug()) printf

static const char DepotCache[] = "/var/cache/house/depot";

static const char *DepotHost = 0;
static const char *DepotRoot = 0;

static int DepotLive = 0;
static unsigned long DepotLatestBackupState = 0;
static char          DepotLatestBackupFile[256] = "";

const char *housedepot_archive_backup (void) {

    unsigned long state = housestate_current (DepotLive);
    if (!DepotLatestBackupFile[0] || (state != DepotLatestBackupState)) {

        time_t timestamp = time (0);
        struct tm now = *localtime (&timestamp);
        char filename[256];
        snprintf (filename, sizeof(filename),
                  "%s/depot-%s-%04d%02d%02d-%02d%02d.tgz",
                  DepotCache, DepotHost,
                  now.tm_year+1900, now.tm_mon+1, now.tm_mday,
                  now.tm_hour, now.tm_min);

        // Refuse to create the same name twice, which means no more than
        // one archive per minute.

        if (!strsame (filename, DepotLatestBackupFile)) {
            // This is a blocking operation, on purpose, because this protects
            // against any change being performed while writing the archive.
            char command[512];
            snprintf (command, sizeof(command),
                      "/usr/bin/tar czf %s -C / %s", filename, DepotRoot+1);
            system (command);

            DepotLatestBackupState = state;
            strtcpy (DepotLatestBackupFile, filename, sizeof(filename));
        }
    }

    int fd = open (DepotLatestBackupFile, O_RDONLY);
    if (fd < 0) return "Archive not created";

    struct stat archivestat;
    if (fstat (fd, &archivestat) < 0) {
        close (fd);
        return "Cannot access archive";
    }
    char *basename = strrchr (DepotLatestBackupFile, '/');
    if (basename) basename += 1;
    else basename = DepotLatestBackupFile; // Should never happen.

    static char disposition[512];
    snprintf (disposition, sizeof(disposition),
              "attachment; filename=\"%s\"", basename);
    echttp_attribute_set ("Content-Disposition", disposition);

    echttp_transfer (fd, archivestat.st_size);

    echttp_content_type_set ("application/gzip");
    return 0;
}

void housedepot_archive_initialize (const char *hostname,
                                    const char *root, int state) {

    static int Initialized = 0;
    if (!Initialized) {
        DepotHost = hostname;
        DepotRoot = root;
        DepotLive = state;
        Initialized = 1;
    }
}

