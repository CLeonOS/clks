#ifndef CLKS_USER_H
#define CLKS_USER_H

#include <clks/types.h>

#define CLKS_USER_NAME_MAX 32U
#define CLKS_USER_HOME_MAX 96U
#define CLKS_USER_HASH_HEX_LEN 64U
#define CLKS_USER_RECORD_MAX 192U
#define CLKS_USER_DB_PATH "/system/users.db"
#define CLKS_USER_ROLE_USER 0ULL
#define CLKS_USER_ROLE_ADMIN 1ULL
#define CLKS_USER_UID_ROOT 0ULL
#define CLKS_USER_UID_BASE 1000ULL
#define CLKS_USER_UID_NOBODY 65534ULL

struct clks_user_public_info {
    u64 uid;
    u64 role;
    u64 logged_in;
    u64 disk_login_required;
    char name[CLKS_USER_NAME_MAX];
    char home[CLKS_USER_HOME_MAX];
};

void clks_user_init(void);
clks_bool clks_user_is_disk_boot(void);
clks_bool clks_user_current_info(struct clks_user_public_info *out_info);
clks_bool clks_user_current_is_admin(void);
clks_bool clks_user_login(const char *name, const char *password, struct clks_user_public_info *out_info);
void clks_user_logout(void);
u64 clks_user_count(void);
clks_bool clks_user_at(u64 index, struct clks_user_public_info *out_info);
clks_bool clks_user_create(const char *name, const char *password, u64 role);
clks_bool clks_user_change_password(const char *name, const char *old_password, const char *new_password);
clks_bool clks_user_set_role(const char *name, u64 role);
clks_bool clks_user_remove(const char *name);
clks_bool clks_user_path_read_allowed(const char *path);
clks_bool clks_user_path_write_allowed(const char *path);
clks_bool clks_user_privileged_operation_allowed(void);

#endif
