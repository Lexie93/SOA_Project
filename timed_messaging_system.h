#ifndef INC_TIMED_MESSAGING_SYSTEM_H
#define INC_TIMED_MESSAGING_SYSTEM_H

#include "constants.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)	MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_dentry->d_inode->i_rdev)
#endif

extern int max_message_size[MINORS];
extern int max_storage_size[MINORS];

#endif /* INC_TIMED_MESSAGING_SYSTEM_H */