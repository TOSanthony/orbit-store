#ifndef ORBIT_INSTALL_H
#define ORBIT_INSTALL_H
/* Written only by startup; copy the result before publishing it to the API. */
const char *orbit_launcher_last_error(void);
const char *orbit_launcher_registration_method(void);
void orbit_launcher_record_error(const char *step, int system_error, int platform_error);
int orbit_launcher_ensure(int state_fd);
/* Removes the home-screen icon this build registered, and only that: a title Orbit does not own is
 * never touched. 1 removed, 0 nothing to do, -1 failed (records kept, so the next start retries). */
int orbit_launcher_remove(int state_fd);
/* Asks the system to uninstall an installed title (its registration and installed data). 0 on success,
 * otherwise a system code (-1 when the entry point is missing). Only a PPSA or CUSA id may be passed. */
int orbit_uninstall_title(const char *title_id);
int orbit_launcher_remove_at(int state_fd, const char *user_parent, int (*prepare)(void),
                             int (*unregister_title)(void));
/* Injectable platform operations permit filesystem tests without a console. */
int orbit_launcher_ensure_at(int state_fd, const char *user_parent, int (*prepare)(void),
                             int (*register_title)(void));
#endif
