/* HouseDepot - a log and ressource file storage service.
 *
 * housedepot_revision.h - A module that hides the details of the RCS interface.
 */

void housedepot_revision_default (const char *arg);

void housedepot_revision_initialize (const char *host,
                                     const char *portal,
                                     int argc, const char *argv[]);

int housedepot_revision_visible (const char *group);

int housedepot_revision_checkout (const char *filename,
                                  const char *revision);

const char *housedepot_revision_checkin (const char *clientname,
                                         const char *filename,
                                         time_t      timestamp,
                                         const char *data, int length);

const char *housedepot_revision_apply (const char *tag,
                                       const char *clientname,
                                       const char *filename,
                                       const char *revision);

const char *housedepot_revision_delete (const char *clientname,
                                        const char *filename,
                                        const char *revision);

const char *housedepot_revision_list (const char *clientname,
                                      const char *dirname);

const char *housedepot_revision_history (const char *clientname,
                                         const char *filename);

void housedepot_revision_prune (const char *clientname,
                                const char *filename, int depth);

void housedepot_revision_repair (const char *dirname);

long long housedepot_revision_get_update_timestamp (void);

