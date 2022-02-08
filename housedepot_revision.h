/* HouseDepot - a log and ressource file storage service.
 *
 * housedepot_revision.h - A module that hides the details of the RCS interface.
 */

void housedepot_revision_initialize (const char *host, const char *portal);

int housedepot_revision_checkout (const char *filename,
                                  const char *revision);

const char *housedepot_revision_checkin (const char *filename,
                                         const char *data, int length);

const char *housedepot_revision_apply (const char *tag,
                                       const char *filename,
                                       const char *revision);

const char *housedepot_revision_delete (const char *filename,
                                        const char *revision);

const char *housedepot_revision_history (const char *uri,
                                         const char *filename);
