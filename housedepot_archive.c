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
 * const char *housedepot_archive_download (const char *name);
 *
 *    Download an existing archive.
 *
 * const char *housedepot_archive_backup (void);
 *
 *    Download the latest archive. A new archive is created if necessary
 *    (i.e. if the state of the repositories has changed since the most
 *    recent backup).
 *
 * const char *housedepot_archive_restore (const char *name);
 *
 *    Restore the specified archive. The existing data is archived (if
 *    necessary) and then wipped out. The archive file must be present in
 *    the depot archive cache.
 *
 *    Return 0 on success, an error string on failure.
 *
 * const char *housedepot_archive_delete (const char *name);
 *
 *    Delete the specified file and return the updated list of archives.
 *
 * const char *housedepot_archive_list (void);
 *
 *    List existing archives as a JSON object. May return an empty string
 *    if there was no update since the latest check, or on error.
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

#include "houselog.h"

#include "housestate.h"
#include "housedepot_archive.h"

#define DEBUG if (housedepot_isdebug()) printf

static const char DepotCache[] = "/var/cache/house/depot";

static const char *DepotHost = 0;
static const char *DepotRoot = 0;

static int DepotLive = 0;
static unsigned long DepotLatestBackupState = 0;
static char          DepotLatestBackupFile[256] = "";

static const char *housedepot_archive_base (const char *path) {

    const char *name = strrchr (path, '/');
    if (name) name += 1;
    else name = path; // Should never happen.
    return name;
}

static void housedepot_archive_save (void) {

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
                      "/usr/bin/tar czf %s -C %s .", filename, DepotRoot);
            system (command);

            const char *name = housedepot_archive_base (filename);
            houselog_event ("ARCHIVE", name, "CREATED", "");

            housestate_changed (DepotLive); // Because an archive was created.
            DepotLatestBackupState = housestate_current (DepotLive);
            strtcpy (DepotLatestBackupFile, filename, sizeof(filename));
        }
    }
}

const char *housedepot_archive_backup (void) {
    housedepot_archive_save ();
    return housedepot_archive_download (0);
}

const char *housedepot_archive_restore (const char *name) {

    housedepot_archive_save ();

    char command[512];
    snprintf (command, sizeof(command),
              "/usr/bin/rm -rf %s/*", DepotRoot);
    system (command);

    snprintf (command, sizeof(command),
              "/usr/bin/tar xf %s/%s -C %s", DepotCache, name, DepotRoot);
    int status = system (command);
    if (status) return "Archive extraction failed";

    houselog_event ("ARCHIVE", name, "RESTORED", "");
    return 0;
}

const char *housedepot_archive_download (const char *name) {

    int fd;
    if (name) {
        char path[256];
        snprintf (path, sizeof(path), "%s/%s", DepotCache, name);
        fd = open (path, O_RDONLY);
    } else {
        name = housedepot_archive_base (DepotLatestBackupFile);
        fd = open (DepotLatestBackupFile, O_RDONLY);
    }
    if (fd < 0) return "Archive not found";

    struct stat archivestat;
    if (fstat (fd, &archivestat) < 0) {
        close (fd);
        return "Cannot access archive";
    }

    static char disposition[512];
    snprintf (disposition, sizeof(disposition),
              "attachment; filename=\"%s\"", name);
    echttp_attribute_set ("Content-Disposition", disposition);

    echttp_transfer (fd, archivestat.st_size);

    echttp_content_type_set ("application/gzip");
    return 0;
}

const char *housedepot_archive_delete (const char *name) {

    if (!name) return "";

    char path[256];
    snprintf (path, sizeof(path), "%s/%s", DepotCache, name);
    unlink (path);
    houselog_event ("ARCHIVE", name, "DELETED", "");

    if (DepotLatestBackupFile[0]) {
        const char *latest = housedepot_archive_base (DepotLatestBackupFile);
        if (strsame (name, latest)) DepotLatestBackupFile[0] = 0;
    }
    housestate_changed (DepotLive); // Because an archive was deleted.

    return housedepot_archive_list ();
}

static int housedepot_archive_filter (const struct dirent *e) {

    const char *suffix = strrchr (e->d_name, '.');
    if (!suffix) return 0;
    if (!strsame (suffix, ".tgz")) return 0;
    return 1;
}

const char *housedepot_archive_list (void) {

    if (housestate_same (DepotLive)) return "";

    static char buffer[16000];

    int cursor = snprintf (buffer, sizeof(buffer),
                           "{\"host\":\"%s\",\"timestamp\":%lld"
                               ",\"latest\":%lu,\"archives\":[",
                           DepotHost, (long long)time(0),
                               housestate_current (DepotLive));

    struct dirent **files = 0;
    int n = scandir (DepotCache, &files, housedepot_archive_filter, alphasort);

    int i;
    const char *prefix = "";
    for (i = 0; i < n; i++) {
         struct dirent *ent = files[i];
         if (ent->d_name[0] == '.') continue; // Skip hidden files, . and ..
         if (ent->d_type != DT_REG) continue; // Skip directories, links, etc.

         cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                             "%s\"%s\"", prefix, ent->d_name);
         prefix = ",";
    }
    for (i = 0; i < n; i++) {
         free (files[i]);
    }
    if (files) free (files);

    snprintf (buffer+cursor, sizeof(buffer)-cursor, "]}");

    echttp_content_type_json();
    return buffer;
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

