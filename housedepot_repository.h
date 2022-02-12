/* HouseDepot - a log and ressource file storage service.
 *
 * housedepot_repository.h - A module to serve revision-controlled pages (files).
 */
void housedepot_repository_initialize (const char *hostname,
                                       const char *portal);

int housedepot_repository_route (const char *uri, const char *path);

