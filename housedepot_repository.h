/* HouseDepot - a log and ressource file storage service.
 *
 * housedepot_repository.h - A module to serve revision-controlled pages (files).
 */
int housedepot_isdebug (void); // FIXME: not in the right place.

void housedepot_repository_initialize (const char *hostname,
                                       const char *portal,
                                       const char *parent);

