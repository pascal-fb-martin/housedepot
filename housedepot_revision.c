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
 * housedepot_revision.c - A module that handles file revisions.
 *
 * DESCRIPTION
 *
 * This module implements a very simplistic source revision system: linear
 * revisions (no branches) and user defined symbolic tags. Each revision is
 * saved as a separate file with the suffix '~'+revision appended to its name.
 * Tags are implemented as symbolic links, with the suffix '~'+tag appended
 * to its name.
 *
 * There are three predefined tags: current, latest and all.
 *
 * To facilitate web access, a symbolic link without suffix always points
 * to the same revision as "~current". (That link is not used here.)
 *
 * The same naming convention is used for all the methods listed below:
 *
 *   clientname: the path as seens by the external client. It is provided
 *               for generating traces, events or fill responses sent
 *               back to the client.
 *
 *   filename:   the path that is used for local storage. This is the name
 *               used for all file operations.
 *
 *   dirname:    the repository root path, as used for local storage.
 *
 * SYNOPSYS
 *
 * void housedepot_revision_initialize (const char *host, const char *portal);
 *
 *   Provides the context to report when formatting responses.
 *
 * int housedepot_revision_checkout (const char *filename,
 *                                   const char *revision);
 *
 *   Checkout the specified revision and make this revision the "current" one.
 *
 * const char *housedepot_revision_checkin (const char *clientname,
 *                                          const char *filename,
 *                                          const char *data, int length);
 *
 *   Checkin the provided data as the new current content of the specified file.
 *
 * const char *housedepot_revision_apply (const char *tag,
 *                                        const char *clientname,
 *                                        const char *filename,
 *                                        const char *revision);
 * 
 *   Apply the specified tag name to the specified revision of the file.
 *   The tag is moved if it was already assigned to another revision.
 *   The tag is created if it did not exist yet.
 *
 * const char *housedepot_revision_delete (const char *clientname,
 *                                         const char *filename,
 *                                         const char *revision);
 *
 *   Delete a specific revision of a file. This automatically deletes all the
 *   user defined tags that link to that revision. This fails and nothing is
 *   deleted if the revision is referenced by a predefined tag.
 *
 * const char *housedepot_revision_list (const char *clientname,
 *                                       const char *dirname);
 *
 *   Return JSON data that lists all the files stored in the repository
 *   identified by its root path.
 *
 * const char *housedepot_revision_history (const char *clientname,
 *                                          const char *filename);
 *
 *   Return JSON data that describes the file history.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <houselog.h>

#include "housedepot_revision.h"

#define FRM '~'

static const char *housedepot_revision_host;
static const char *housedepot_revision_portal;

static char housedepot_valid_revision[256] = {0};

void housedepot_revision_initialize (const char *host, const char *portal) {

    housedepot_revision_host = host;
    housedepot_revision_portal = portal;

    // Precalculate which characters are valid in a revision or tag:
    int i;
    for (i = 0; i < 128; i++) {
        if (isalnum(i)) housedepot_valid_revision[i] = 1;
    }
    housedepot_valid_revision['.'] = 1;
    housedepot_valid_revision['_'] = 1;
    housedepot_valid_revision['-'] = 1;
}

static int housedepot_revision_isvalid (const char *revision) {

    if (!revision) return 0;
    while (*revision) {
        if (!housedepot_valid_revision[(unsigned char)(*revision)])
            return 0;
        revision += 1;
    }
    return 1;
}

static void housedepot_trace (const char *source, int line, const char *level,
                              const char *path,
                              const char *action,
                              const char *from, const char *to) {

    const char *basename = strrchr (path, '/');

    basename = basename ? basename + 1 : path;
    if (to) {
        houselog_trace (source, line, level, basename,
                        "%s %s TO %s", action, from, to);
    } else
        houselog_trace (source, line, level, basename, "%s %s", action, from);
}

int housedepot_revision_checkout (const char *filename,
                                  const char *revision) {
    char fullname[1024];

    if (!housedepot_revision_isvalid(revision)) return -1;

    snprintf (fullname, sizeof(fullname), "%s%c%s", filename, FRM, revision);
    return open (fullname, O_RDONLY);
}

static int housedepot_revision_link (const char *target, const char *link) {
    unlink (link);
    return symlink(target, link);
}

const char *housedepot_revision_checkin (const char *clientname,
                                         const char *filename,
                                         const char *data, int length) {
    int pathsz;
    int newrev;
    char fullname[1024];
    char link[1024];

    const char *basename = strrchr(filename, '/');
    if (!basename) return "invalid file path";
    if (!strcmp(basename, "/all")) return "invalid file name";

    if (strchr(filename, FRM)) return "invalid character in name";

    // Retrieve which revision number to use for this new file revision.
    // (Increment latest.)
    //
    snprintf (link, sizeof(link), "%s%c%s", filename, FRM, "latest");
    pathsz = readlink (link, fullname, sizeof(fullname));
    if (pathsz <= 0) {
        newrev = 1;
    } else {
        fullname[pathsz] = 0;
        housedepot_trace (HOUSE_INFO, filename, "FOUND", "latest", fullname);
        char *rev = strrchr (fullname, FRM);
        if (!rev) return "invalid revision database";
        newrev = atoi (rev+1) + 1;
        if (newrev <= 1) return "invalid revision number";
    }

    // Create the (real) file.
    //
    snprintf (fullname, sizeof(fullname), "%s%c%d", filename, FRM, newrev);
    housedepot_trace (HOUSE_INFO, filename, "NEW", "REVISION", fullname);
    int fd = open (fullname, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) return "Cannot open for writing";
    if (write (fd, data, length) != length) return "Cannot write the data";
    close(fd);

    // Set the standard tags as symbolic links: ~latest and ~current.
    //
    housedepot_trace (HOUSE_INFO, filename, "UPDATE", "latest", fullname);
    if (housedepot_revision_link (fullname, link))
        return "Cannot create link for the latest tag";

    housedepot_trace (HOUSE_INFO, filename, "UPDATE", "current", fullname);
    snprintf (link, sizeof(link), "%s%c%s", filename, FRM, "current");
    if (housedepot_revision_link (fullname, link))
        return "Cannot create link for the current tag";

    if (housedepot_revision_link (fullname, filename))
        return "Cannot create link for default file";

    houselog_event ("FILE", clientname, "CHECKED IN", "AS REVISION %d", newrev);

    return 0;
}

static int housedepot_revision_resolve (const char *filename, const char *tag,
                                        char *result, int size) {

    if (! housedepot_revision_isvalid(tag)) return 0;

    // Eliminate any existing revision/tag suffix.
    //
    snprintf (result, size, "%s", filename);
    char *sep = strrchr (result, FRM);
    if (sep) *sep = 0;

    // Convert the tag name to a revision and append to the file name.
    //
    if (isdigit(tag[0])) {
        int cursor = strlen(result);
        snprintf (result+cursor, size-cursor, "%c%s", FRM, tag);
    } else {
        char link[1024];
        snprintf (link, sizeof(link), "%s%c%s", result, FRM, tag);
        int pathsz = readlink (link, result, size-1);
        if (pathsz <= 0) return 0;
        result[pathsz] = 0;
    }
    return 1;
}

const char *housedepot_revision_apply (const char *tag,
                                       const char *clientname,
                                       const char *filename,
                                       const char *revision) {
    char fullname[1024];
    char link[1024];

    if (! housedepot_revision_isvalid(tag)) return "invalid tag name";
    if (isdigit(tag[0])) return "invalid numeric tag name";
    if (!strcmp(tag, "all")) return "cannot assign the all tag name";
    if (!strcmp(tag, "latest")) return "cannot assign the latest tag name";

    if (! housedepot_revision_resolve (filename, revision?revision:"current",
                                       fullname, sizeof(fullname)))
        return "invalid revision";

    housedepot_trace (HOUSE_INFO, filename, "APPLY", tag, fullname);

    snprintf (link, sizeof(link), "%s%c%s", filename, FRM, tag);
    if (housedepot_revision_link (fullname, link))
        return "Cannot create the tag link";

    if (!strcmp (tag, "current")) {
        // Create the link for the GET target, i.e. the name without revision.
        if (housedepot_revision_link (fullname, filename))
            return "Cannot create link for default file";
    }

    const char *realrev = strrchr (fullname, FRM);
    if (!realrev) realrev = "~(invalid)"; // Thou shall not crash.
    houselog_event ("FILE", clientname, "APPLIED",
                    "TAG %s TO REVISION %s", tag, realrev+1);

    return 0;
}

static char *scandir_pattern = 0;
static int scandir_pattern_length = 0;

static int housedepot_revision_filter (const struct dirent *e) {
    if (!scandir_pattern) return 1; // Should never happen, avoid crash.
    if (strncmp (e->d_name, scandir_pattern, scandir_pattern_length)) return 0;
    return 1;
}

static int housedepot_revision_compare (const struct dirent **a,
                                        const struct dirent **b) {

    if ((*a)->d_type == DT_DIR) {
        if ((*b)->d_type != DT_DIR) return 1; // Subdirectories last.
        return 0;
    }
    if ((*b)->d_type == DT_DIR) return -1; // Subdirectories last.

    const char *aname = strrchr((*a)->d_name, FRM);
    const char *bname = strrchr((*b)->d_name, FRM);

    if (!aname) return 0;
    if (!bname) return 0;
    aname += 1;
    bname += 1;

    if (isdigit(aname[0])) {
        if (!isdigit(bname[0])) return 1; // Tags before revision (a > b).
        int reva = atoi(aname);
        int revb = atoi(bname);
        if (reva < revb) return -1;
        if (reva > revb) return 1;
        return 0; // Should never happen (two file with the same name?)
    }

    if (isdigit(bname[0])) return -1; // Tags before revision (a < b).
    return alphasort(a, b);
}

static void housedepot_revision_cleanscan (struct dirent **files, int n) {
    int i;
    for (i = 0; i < n; i++) {
        free (files[i]);
    }
    free (files);
}

const char *housedepot_revision_delete (const char *clientname,
                                        const char *filename,
                                        const char *revision) {

    char fullname[1024];
    char working[1024];

    if (!revision) return "revision is required";
    snprintf (fullname, sizeof(fullname), "%s%c%s", filename, FRM, revision);

    if (!isdigit(revision[0])) {
        // This operation is about deleting a tag.
        if (!strcmp(revision, "current")) return "Cannot delete current";
        if (!strcmp(revision, "latest")) return "Cannot delete latest";
        unlink (fullname);
        houselog_event ("FILE", clientname, "REMOVED", "TAG %s", revision);
        return 0;
    }

    // Now the revision is a real revision, not a tag.
    // Protect the latest and current revisions against deletion.
    //
    if (! housedepot_revision_resolve (filename, "current",
                                       working, sizeof(working)))
        return "broken current tag";
    if (!strcmp (fullname, working)) return "cannot delete current";

    if (! housedepot_revision_resolve (filename, "latest",
                                       working, sizeof(working)))
        return "broken latest tag";
    if (!strcmp (fullname, working)) return "cannot delete latest";

    // So this revision is neither the latest or the current revision.
    // Now we must retrieve all tags that refer to this, and delete
    // them first.
    // Retrieve both the directory to scan and the file pattern to search.
    //
    struct dirent **files;

    snprintf (working, sizeof(working), "%s%c", filename, FRM);
    scandir_pattern = strrchr (working, '/');
    if (!scandir_pattern) return 0;
    *(scandir_pattern++) = 0;
    scandir_pattern_length = strlen(scandir_pattern);

    int n = scandir (working, &files, housedepot_revision_filter,
                                      housedepot_revision_compare);
    scandir_pattern = 0;

    int i;
    for (i = 0; i < n; i++) {
        if (files[i]->d_type == DT_LNK) {
            char link[1024];
            char target[1024];
            snprintf (link, sizeof(link), "%s/%s", working, files[i]->d_name);
            int pathsz = readlink (link, target, sizeof(target)-1);
            if (pathsz <= 0) continue;
            target[pathsz] = 0;
            char *targetsep = strrchr (target, FRM);
            if (targetsep) {
                if (!strcmp (targetsep+1, revision)) {
                    housedepot_trace
                        (HOUSE_INFO, filename, "DELETE", files[i]->d_name, 0);
                    unlink(link);
                    const char *tag = strrchr (files[i]->d_name, FRM);
                    if (!tag) tag = "~(invalid)"; // Do not crash.
                    houselog_event ("FILE", clientname, "DELETED", "TAG %s", tag+1);
                }
            }
        }
    }
    housedepot_revision_cleanscan (files, n);

    // Now that all tag pointing to this revision were removed, we can delete
    // the revision file itself.
    //
    housedepot_trace (HOUSE_INFO, filename, "DELETE", fullname, 0);
    unlink(fullname);

    const char *realrev = strrchr (fullname, FRM);
    if (!realrev) realrev = "~(invalid)"; // Thou shall not crash.
    houselog_event ("FILE", clientname, "DELETED", "REVISION %s", realrev+1);

    return 0;
}

const char *housedepot_revision_list (const char *clientname,
                                      const char *dirname) {

    static char buffer[16000];

    int i;
    struct dirent **files;

    int cursor = snprintf (buffer, sizeof(buffer),
                           "{\"host\":\"%s\",\"timestamp\":%d",
                           housedepot_revision_host, (int)time(0));
    if (housedepot_revision_portal)
        cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                           ",\"proxy\":\"%s\"", housedepot_revision_portal);
    cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor, ",\"files\":[");

    int n = scandir (dirname, &files, 0, housedepot_revision_compare);
    const char *sep = "";
    int tags = 1;

    for (i = 0; i < n; i++) {
        struct dirent *ent = files[i];
        if (ent->d_name[0] == '.') continue; // Skip hidden files, . and ..

        switch (ent->d_type) {
        case DT_DIR:
            { // This block is a workaround for errors with some GCC versions.
            // Support only one level of subdirectory (see README.md)
            int i2;
            struct dirent **files2;
            static char subdir[1024];
            snprintf (subdir, sizeof(subdir), "%s/%s", dirname, ent->d_name);
            int n2 = scandir (subdir, &files2, 0, housedepot_revision_compare);
            if (n2 <= 0) break;

            for (i2 = 0; i2 < n2; i2++) {
                struct dirent *ent2 = files2[i2];
                if (ent2->d_type != DT_LNK) continue; // One directory level.
                if (strchr(ent2->d_name, FRM)) continue; // Skip tag links.
                char link[1024];
                char target[1024];
                snprintf (link, sizeof(link), "%s/%s/%s",
                          dirname, ent->d_name, ent2->d_name);
                int pathsz = readlink (link, target, sizeof(target)-1);
                if (pathsz <= 0) continue;
                target[pathsz] = 0;
                char *rev = strrchr(target, FRM);
                if (!rev) continue;
                struct stat fs;
                if (stat (target, &fs) != 0) continue;
                cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                                    "%s{\"name\":\"%s/%s/%s\",\"rev\":\"%s\",\"time\":%d}",
                                    sep, clientname, ent->d_name, ent2->d_name, rev+1, (int)(fs.st_mtime));

                sep = ",";
            }
            housedepot_revision_cleanscan (files2, n2);
            }
            break;

        case DT_LNK:
            if (strchr(ent->d_name, FRM)) break; // Skip tag links.
            char link[1024];
            char target[1024];
            snprintf (link, sizeof(link), "%s/%s", dirname, ent->d_name);
            int pathsz = readlink (link, target, sizeof(target)-1);
            if (pathsz <= 0) break;
            target[pathsz] = 0;
            char *rev = strrchr(target, FRM);
            if (!rev) break;
            struct stat fs;
            if (stat (target, &fs) != 0) break;
            cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                                "%s{\"name\":\"%s/%s\",\"rev\":\"%s\",\"time\":%d}",
                                sep, clientname, ent->d_name, rev+1, (int)(fs.st_mtime));
            sep = ",";
            break;

        default:
            // Ignore actual files: we are not asking for all revisions.
            break;
        }
    }
    snprintf (buffer+cursor, sizeof(buffer)-cursor, "]}");
    if (n > 0) housedepot_revision_cleanscan (files, n);

    return buffer;
}

const char *housedepot_revision_history (const char *clientname,
                                         const char *filename) {

    static char buffer[16000];

    int i;
    static char dirname[1024];
    struct dirent **files;

    // Retrieve both the directory to scan and the file pattern to search.
    //
    snprintf (dirname, sizeof(dirname), "%s%c", filename, FRM);
    scandir_pattern = strrchr (dirname, '/');
    if (!scandir_pattern) return 0;
    *(scandir_pattern++) = 0;
    scandir_pattern_length = strlen(scandir_pattern);

    int n = scandir (dirname, &files, housedepot_revision_filter,
                                      housedepot_revision_compare);
    scandir_pattern = 0;

    int cursor = snprintf (buffer, sizeof(buffer),
                           "{\"host\":\"%s\",\"timestamp\":%d,\"file\":\"%s\"",
                           housedepot_revision_host, (int)time(0), clientname);
    if (housedepot_revision_portal)
        cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                           ",\"proxy\":\"%s\"", housedepot_revision_portal);
    cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor, ",\"tags\":[");

    const char *sep = "";
    int tags = 1;

    for (i = 0; i < n; i++) {
        struct dirent *ent = files[i];

        switch (ent->d_type) {
        case DT_LNK:
            if (tags) {
                const char *tagname = strrchr(ent->d_name, FRM);
                char fullname[1024];
                if (tagname &&
                    housedepot_revision_resolve (filename, tagname+1,
                                                 fullname, sizeof(fullname))) {
                    const char *rev = strrchr (fullname, FRM);
                    if (rev) {
                        cursor +=
                            snprintf (buffer+cursor, sizeof(buffer)-cursor,
                                      "%s[\"%s\",%s]", sep, tagname+1, rev+1);
                        sep = ",";
                    }
                }
            }
            break;
        case DT_REG:
            if (tags) {
                cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                                    "],\"history\":[");
                sep = "";
                tags = 0; // No more link representing tags.
            }
            const char *ver = strrchr(ent->d_name, FRM);
            if (ver) {
                char fullname[1024];
                struct stat filestat;
                snprintf (fullname, sizeof(fullname),
                          "%s/%s", dirname, ent->d_name);
                if (stat (fullname, &filestat) == 0) {
                    cursor += 
                        snprintf (buffer+cursor, sizeof(buffer)-cursor,
                                  "%s{\"rev\":%s,\"time\":%d}",
                                  sep, ver+1, filestat.st_mtime);
                    sep = ",";
                }
            }
            break;
        default:
            break;
        }
    }
    snprintf (buffer+cursor, sizeof(buffer)-cursor, "]}");

    if (n > 0) housedepot_revision_cleanscan (files, n);
    return buffer;
}

