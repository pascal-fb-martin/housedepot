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
 * void housedepot_archive_initialize (const char *hostname,
 *                                     const char *root, int state);
 *
 *    Set the host name and paths, create the HTTP endpoints for this module.
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
#include "housedepot_revision.h"
#include "housedepot_repository.h"
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

static const char *housedepot_archive_extract (const char *name) {

    char command[512];
    snprintf (command, sizeof(command), "/usr/bin/rm -rf %s/*", DepotRoot);
    system (command);

    snprintf (command, sizeof(command),
              "/usr/bin/tar xf %s/%s -C %s", DepotCache, name, DepotRoot);
    if (system (command)) return "Archive extraction failed";
    return 0;
}

static const char *housedepot_archive_download (const char *name) {

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

static const char *housedepot_archive_restore (const char *action,
                                               const char *uri,
                                               const char *data, int length) {

    const char *name = echttp_parameter_get ("name");
    if (!name) {

        echttp_error (500, "Missing archive name");

    } else if (strsame (action, "GET")) {

        housedepot_archive_save (); // Protect the existing repository content

        const char *error = housedepot_archive_extract (name);
        if (error) {
            echttp_error (500, error);
            return "";
        }

        houselog_event ("ARCHIVE", name, "RESTORED", "");
        housedepot_repository_reload (DepotRoot);
        housedepot_revision_reload ();

    } else {
        echttp_error (400, "Invalid method");
    }
    return "";
}

static const char *housedepot_archive_backup (const char *action,
                                              const char *uri,
                                              const char *data, int length) {

    const char *name = echttp_parameter_get ("name");

    if (!name) housedepot_archive_save ();
    const char *error = housedepot_archive_download (name);

    if (error) echttp_error (500, error);
    return "";
}

static int housedepot_archive_filter (const struct dirent *e) {

    const char *suffix = strrchr (e->d_name, '.');
    if (!suffix) return 0;
    if (!strsame (suffix, ".tgz")) return 0;
    return 1;
}

static const char *housedepot_archive_list (const char *action,
                                            const char *uri,
                                            const char *data, int length) {

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

static const char *housedepot_archive_delete (const char *action,
                                              const char *uri,
                                              const char *data, int length) {

    const char *name = echttp_parameter_get ("name");
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

    return housedepot_archive_list (action, uri, data, length);
}

static int housedepot_archive_file (const char *name) {

    // Never accept to create a file based on a client-provided path.
    const char *basename = strrchr (name, '/');
    if (basename) basename += 1;
    else          basename = name;

    char path[256];
    snprintf (path, sizeof(path), "%s/%s", DepotCache, basename);

    int fd = open (path, O_CREAT+O_WRONLY, 0644);
    if (fd < 0) echttp_error (500, "Failed to open the local archive");
    else housestate_changed (DepotLive); // Because an archive was created

    return fd;
}

static const char *housedepot_archive_upload (const char *action,
                                              const char *uri,
                                              const char *data, int length) {

    if (length <= 0) return ""; // Nothing to upload, or it was asynchronous.

    const char *name = echttp_parameter_get ("name");
    if (!name) return "";

    int fd = housedepot_archive_file (name);
    if (fd < 0) return "";

    if (write (fd, data, length) < 0)
        echttp_error (500, "Failed to write to the local archive");

    close (fd);
    houselog_event ("ARCHIVE", name, "UPLOADED", "synchronously");

    return "";
}

static const char *housedepot_archive_ready (const char *action,
                                             const char *uri,
                                             const char *data, int length) {

    const char *name = echttp_parameter_get ("name");
    if (!name) return 0;

    const char *ascii = echttp_attribute_get ("Content-Length");
    if (!ascii) return 0;

    int fd = housedepot_archive_file (name);
    if (fd < 0) return 0;

    if (length > 0) {
        if (write (fd, data, length) < 0) {
            echttp_error (500, "Failed to write to the local archive");
            return 0;
        }
    }

    int total = atoi (ascii);
    if (total > length) {
        echttp_transfer (fd, total - length);
    } else {
        close (fd);
    }
    houselog_event ("ARCHIVE", name, "UPLOADED", "asynchronously");
    return 0;
}

void housedepot_archive_initialize (const char *hostname,
                                    const char *root, int state) {

    static int Initialized = 0;
    if (!Initialized) {
        DepotHost = hostname;
        DepotRoot = root;
        DepotLive = state;

        echttp_route_uri ("/depot/backup", housedepot_archive_backup);
        echttp_route_uri ("/depot/restore", housedepot_archive_restore);
        echttp_route_uri ("/depot/archive/all", housedepot_archive_list);
        echttp_route_uri ("/depot/archive/delete", housedepot_archive_delete);

        echttp_asynchronous_route (echttp_route_uri ("/depot/archive/upload",
                                                     housedepot_archive_upload),
                                   housedepot_archive_ready);
        Initialized = 1;
    }
}

