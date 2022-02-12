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
 *
 * housedepot.c - Main loop of the housedepot program.
 *
 * SYNOPSYS:
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"
#include "echttp_cors.h"
#include "echttp_json.h"
#include "echttp_static.h"
#include "houseportalclient.h"
#include "houselog.h"
#include "houseconfig.h"

#include "housedepot_revision.h"
#include "housedepot_repository.h"

static int use_houseportal = 0;

static void housedepot_background (int fd, int mode) {

    static time_t LastFlush = 0;
    static time_t LastRenewal = 0;
    time_t now = time(0);

    if (use_houseportal) {
        static const char *path[] = {"depot:/depot"};
        if (now >= LastRenewal + 60) {
            if (LastRenewal > 0)
                houseportal_renew();
            else
                houseportal_register (echttp_port(4), path, 1);
            LastRenewal = now;
        }
    }
}

static void housedepot_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    int i;
    const char *error;

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        houseportal_initialize (argc, argv);
        use_houseportal = 1;
    }
    houselog_initialize ("depot", argc, argv);

    echttp_cors_allow_method("GET");
    echttp_protect (0, housedepot_protect);

    housedepot_revision_initialize (houselog_host(), houseportal_server());
    housedepot_repository_initialize (houselog_host(), houseportal_server());

    housedepot_repository_route ("/depot/config", "/var/lib/house/config");
    housedepot_repository_route ("/depot/scripts", "/var/lib/house/scripts");

    for (i = 1; i < argc; ++i) {
        const char *map = echttp_option_match ("-repo=", argv[i], 0);
        if (map) {
            char uri[512];
            char *path;
            snprintf (uri, sizeof(uri), "/depot/%s", map);
            path = strchr (uri, ':');
            if (path) {
                *(path++) = 0;
                housedepot_repository_route (uri, path);
                if (echttp_isdebug())
                    printf ("Add repository %s -> %s\n", uri, path);
            }
        }
    }

    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&housedepot_background);
    houselog_event ("SERVICE", "depot", "STARTED", "ON %s", houselog_host());
    echttp_loop();
}

