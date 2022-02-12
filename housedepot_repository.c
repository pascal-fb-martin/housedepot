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
 * void housedepot_repository_route (const char *uri, const char *path);
 *
 *    Initialize the module and its web API.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "echttp.h"
#include "echttp_static.h"
#include "echttp_catalog.h"

#include "housedepot_revision.h"
#include "housedepot_repository.h"

static echttp_catalog housedepot_repository_roots;

static echttp_catalog housedepot_repository_type;

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
    char rooturi[1024];
    char filename[1024];
    const char * error;

    if (strstr(uri, "../")) {
        if (echttp_isdebug()) printf ("Security violation: %s\n", uri);
        echttp_error (406, "Not Acceptable");
        return "";
    }

    strncpy (rooturi, uri, sizeof(rooturi)); // Make a writable copy.
    rooturi[sizeof(rooturi)-1] = 0;

    for(;;) {
        if (echttp_isdebug()) printf ("Searching static map for %s\n", rooturi);
        path = echttp_catalog_get (&housedepot_repository_roots, rooturi);
        if (path) break;
        char *sep = strrchr (rooturi+1, '/');
        if (sep == 0) break;
        *sep = 0;
    }
    if (path == 0) {
        echttp_error (404, "Path not found"); // Should never happen, but.
        return "";
    }
    if (echttp_isdebug()) printf ("found match for %s: %s\n", rooturi, path);

    size_t pathlen = strlen(path);
    if (pathlen >= sizeof(filename)) {
        echttp_error (500, "Buffer overflow"); // Should never happen, but.
        return "";
    }
    strncpy (filename, path, sizeof(filename));
    strncpy (filename+pathlen, uri+strlen(rooturi), sizeof(filename)-pathlen);
    filename[sizeof(filename)-1] = 0;

    const char *revision = echttp_parameter_get ("revision");

    if (!strcmp (action, "GET")) {
        if (!revision)
            revision = "current";
        else if (!strcmp (revision, "all")) {
            echttp_content_type_json();
            const char *data = housedepot_revision_history (uri, filename);
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

    if (!strcmp (action, "PUT")) {
        snprintf (rooturi, sizeof(rooturi), "%s", filename);
        char *subdir = strrchr (rooturi, '/');
        if (subdir) {
            *subdir = 0;
            if (mkdir (rooturi, 0700) < 0) {
                if (errno != EEXIST) {
                    echttp_error (500, "URI too deep");
                    return "";
                }
            }
        }
        error = housedepot_revision_checkin (uri, filename, data, length);
        if (error) echttp_error (500, error);
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
        error = housedepot_revision_apply (tag, uri, filename, revision);
        if (error) echttp_error (500, error);
        return "";
    }

    if (!strcmp (action, "DELETE")) {
        if (!revision) {
            echttp_error (403, "Revision to delete not specified");
            return "";
        }
        error = housedepot_revision_delete (uri, filename, revision);
        if (error) echttp_error (500, error);
        return "";
    }

    // No other method is allowed.
    echttp_error (405, "Method Not Allowed");
    return "";
}

static void housedepot_repository_initialize (void) {

    static int Initialized = 0;
    if (!Initialized) {
        // Catalog the supported content types.
        int i;
        for (i = 0; housedepot_repository_supported[i].extension; ++i) {
            echttp_catalog_set (&housedepot_repository_type,
                                housedepot_repository_supported[i].extension,
                                housedepot_repository_supported[i].content);
        }
        Initialized = 1;
    }
}

int housedepot_repository_route (const char *uri, const char *path) {

    housedepot_repository_initialize();
    echttp_catalog_set (&housedepot_repository_roots, uri, path);
    return echttp_route_match (uri, housedepot_repository_page);
}

