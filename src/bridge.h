#ifndef ONET_BRIDGE_H
#define ONET_BRIDGE_H

/* Create a Linux bridge `name`, then add each iface in members_csv as a member.
 * Members are comma-separated; whitespace tolerated. Idempotent on existing
 * bridge (member adds re-run cleanly). Returns 0 on success. */
int bridge_create(const char *name, const char *members_csv);

/* Take the bridge down and delete it. Members are released by the kernel. */
int bridge_destroy(const char *name);

#endif /* ONET_BRIDGE_H */
