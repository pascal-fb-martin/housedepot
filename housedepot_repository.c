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
 * housedepot_repository.c: a module to manage web resources (files) and maintain
 * their change history.
 *
 * void housedepot_repository_initialize (const char *hostname,
 *                                        const char *portal,
 *                                        const char *parent);
 *
 *    Set the host and portal names, initialize the module's resources and
 *    initialize the context for each repository found.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "echttp.h"
#include "echttp_static.h"
#include "echttp_catalog.h"

#include "housedepot_revision.h"
#include "housedepot_repository.h"

#define DEBUG if (housedepot_isdebug()) printf

static echttp_catalog housedepot_repository_roots;
static echttp_catalog housedepot_repository_depth;

static echttp_catalog housedepot_repository_type;

static const char *housedepot_repository_host;
static const char *housedepot_repository_portal;

/* List the supported content types.
 * Only list text-based format here: RCS does not handle binary data.
 */
static struct {
    const char *extension;
    const char *content;
} housedepot_repository_supported[] = {
    {"html", "text/html"},
    {"htm",  "text/html"},
    {"css",  "text/css"},
    {"csv",  "text/csv"},
    {"json", "application/json"},
    {"jsn",  "application/json"},
    {"js",   "application/javascript"},
    {"xml",  "text/xml"},
    {"txt",  "text/plain"},
    {0, 0}
};

static const char *housedepot_repository_transfer (int fd,
                                                   const char *filename,
                                                   const char *revision) {

    if (fd < 0) {
        echttp_error (404, "Not found");
        return "";
    }
    struct stat fileinfo;
    if (fstat(fd, &fileinfo) < 0) goto unsupported;
    if ((fileinfo.st_mode & S_IFMT) != S_IFREG) goto unsupported;
    if (fileinfo.st_size < 0) goto unsupported;

    if (echttp_isdebug()) {
        if (revision)
            printf ("Serving static file: %s (rev %s)\n", filename, revision);
        else
            printf ("Serving static file: %s\n", filename);
    }

    const char *sep = strrchr (filename, '.');
    if (sep) {
        const char *content = echttp_catalog_get (&housedepot_repository_type, sep+1);
        if (content) {
            echttp_content_type_set (content);
        }
    }
    echttp_transfer (fd, fileinfo.st_size);
    return "";

unsupported:
    echttp_error (406, "Not Acceptable");
    close(fd);
    return "";
}

static const char *housedepot_repository_page (const char *action,
                                               const char *uri,
                                               const char *data, int length) {
    const char *path;
    char localuri[1024];
    char rooturi[1024];
    char filename[1024];
    const char * error;
    int is_all = 0;

    if (strstr(uri, "../")) {
        DEBUG ("Security violation: %s\n", uri);
        echttp_error (406, "Not Acceptable");
        return "";
    }

    snprintf(localuri, sizeof(localuri), "%s", uri); // Make a writable copy.

    // Detect the /all terminator and consume it.
    //
    char *base = strrchr (localuri, '/');
    if (base && (!strcmp (base, "/all"))) {
        is_all = 1;
        *base = 0;
        DEBUG ("List request for %s\n", localuri);
    }

    snprintf(rooturi, sizeof(rooturi), "%s", localuri);
    for(;;) {
        DEBUG ("Searching static map for %s\n", rooturi);
        path = echttp_catalog_get (&housedepot_repository_roots, rooturi);
        if (path) break;
        char *sep = strrchr (rooturi+1, '/');
        if (sep == 0) break;
        if (!housedepot_revision_authority (sep+1)) break; // Pretend not found
        *sep = 0;
    }
    if (path == 0) {
        echttp_error (404, "Path not found"); // Should never happen, but.
        return "";
    }
    DEBUG ("found match for %s: %s\n", rooturi, path);

    size_t pathlen = strlen(path);
    if (pathlen >= sizeof(filename)) {
        echttp_error (500, "Buffer overflow"); // Should never happen, but.
        return "";
    }
    snprintf (filename, sizeof(filename),
              "%s%s", path, localuri+strlen(rooturi));

    const char *revision = echttp_parameter_get ("revision");

    if (!strcmp (action, "GET")) {
        if (is_all) {
            echttp_content_type_json();
            return housedepot_revision_list (localuri, filename);
        }
        if (!revision)
            revision = "current";
        else if (!strcmp (revision, "all")) {
            echttp_content_type_json();
            const char *data = housedepot_revision_history (localuri, filename);
            if (!data) {
                echttp_error (404, "Not found");
                return "";
            }
            return data;
        }
        int size;
        int fd = housedepot_revision_checkout (filename, revision);
        return housedepot_repository_transfer (fd, filename, revision);
    }

    if (is_all) {
        echttp_error (500, "Invalid URI"); // Only valid in GET method.
        return "";
    }

    if (!strcmp (action, "PUT")) {
        char parent[1024];
        snprintf (parent, sizeof(parent), "%s", filename);
        char *subdir = strrchr (parent, '/');
        if (subdir) {
            *subdir = 0;
            if (mkdir (parent, 0750) < 0) {
                if (errno != EEXIST) {
                    echttp_error (500, "URI too deep");
                    return "";
                }
            }
        }
        time_t timestamp = 0;
        const char *timestampstring = echttp_parameter_get ("time");
        if (timestampstring) timestamp = atoll(timestampstring);

        error = housedepot_revision_checkin
                   (localuri, filename, timestamp, data, length);
        if (error) echttp_error (500, error);

        const char *depth = echttp_catalog_get (&housedepot_repository_depth, rooturi);
        if (depth) {
           housedepot_revision_prune (localuri, filename, atoi(depth));
        }
        return "";
    }

    if (!strcmp (action, "POST")) {
        const char *tag = echttp_parameter_get ("tag");
        if ((!tag) && (!revision)) return ""; // No operation.
        if (!tag) tag = "current";
        if (!revision) revision = "current";
        if (!strcmp (revision, "all")) {
            echttp_error (400, "invalid tag name");
            return "";
        }
        error = housedepot_revision_apply (tag, localuri, filename, revision);
        if (error) echttp_error (500, error);
        return "";
    }

    if (!strcmp (action, "DELETE")) {
        if (!revision) {
            echttp_error (403, "Revision to delete not specified");
            return "";
        }
        error = housedepot_revision_delete (localuri, filename, revision);
        if (error) echttp_error (500, error);
        return "";
    }

    // No other method is allowed.
    echttp_error (405, "Method Not Allowed");
    return "";
}

static char housedepot_repositories[65537];
static int housedepot_repositories_cursor;
static char *housedepot_repositories_sep;

static int housedepot_repository_list_iterator (const char *name,
                                                const char *value) {

    housedepot_repositories_cursor +=
        snprintf (housedepot_repositories+housedepot_repositories_cursor,
                  sizeof(housedepot_repositories)-housedepot_repositories_cursor,
                  "%s\"%s\"", housedepot_repositories_sep, name);

    housedepot_repositories_sep = ",";
    return 0;
}

static const char *housedepot_repository_list (const char *action,
                                               const char *uri,
                                               const char *data, int length) {

    char *buffer = housedepot_repositories;
    const int size = sizeof(housedepot_repositories);

    int cursor = snprintf (buffer, size,
                           "{\"host\":\"%s\",\"timestamp\":%d",
                           housedepot_repository_host, (int)time(0));
    if (housedepot_repository_portal)
        cursor += snprintf (buffer+cursor, size-cursor,
                           ",\"proxy\":\"%s\"", housedepot_repository_portal);
    cursor += snprintf (buffer+cursor, size-cursor, ",\"repositories\":[");

    housedepot_repositories_cursor = cursor;
    housedepot_repositories_sep = "";
    echttp_catalog_enumerate (&housedepot_repository_roots,
                              housedepot_repository_list_iterator);

    cursor = housedepot_repositories_cursor;
    snprintf (buffer+cursor, size-cursor, "]}");
    echttp_content_type_json();
    return buffer;
}

static const char *housedepot_repository_check (const char *action,
                                                const char *uri,
                                                const char *data, int length) {

    snprintf (housedepot_repositories, sizeof(housedepot_repositories),
              "{\"host\":\"%s\",\"timestamp\":%d,\"updated\":%lld}",
              housedepot_repository_host, (int)time(0),
              housedepot_revision_get_update_timestamp());
    echttp_content_type_json();
    return housedepot_repositories;
}

static int housedepot_repository_route (const char *uri, const char *path) {

    echttp_catalog_set (&housedepot_repository_roots, uri, path);
    char options[256];
    snprintf (options, sizeof(options), "%s/.options", path);
    FILE *file = fopen (options, "r");
    if (file) {
       while (fgets (options, sizeof(options), file)) {
          if (strstr (options, "depth ") == options) {
             echttp_catalog_set (&housedepot_repository_depth, uri, strdup(options+6));
          }
       }
       fclose (file);
    }
    return echttp_route_match (uri, housedepot_repository_page);
}

void housedepot_repository_initialize (const char *hostname,
                                       const char *portal,
                                       const char *parent) {

    static int Initialized = 0;
    if (!Initialized) {
        // Catalog the supported content types.
        int i;
        for (i = 0; housedepot_repository_supported[i].extension; ++i) {
            echttp_catalog_set (&housedepot_repository_type,
                                housedepot_repository_supported[i].extension,
                                housedepot_repository_supported[i].content);
        }
        housedepot_repository_host = hostname;
        housedepot_repository_portal = portal;
        echttp_route_uri ("/depot/all", housedepot_repository_list);
        echttp_route_uri ("/depot/check", housedepot_repository_check);

        // Find out all the repositories and initialize them.
        struct dirent **files = 0;
        int n = scandir (parent, &files, 0, 0);
        for (i = 0; i < n; i++) {
           struct dirent *ent = files[i];
           if (ent->d_name[0] == '.') continue; // Skip hidden entries.
           if (ent->d_type != DT_DIR) continue; // Must be a directory.
           char uri[256];
           char path[256];
           snprintf (uri, sizeof(uri), "/depot/%s", ent->d_name);
           snprintf (path, sizeof(path), "%s/%s", parent, ent->d_name);
           housedepot_repository_route (strdup(uri), strdup(path));
           free (ent);
           housedepot_revision_repair (path);
        }
        if (files) free (files);
        Initialized = 1;
    }
}


