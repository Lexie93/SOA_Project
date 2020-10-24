#ifndef INC_CONSTANTS_H
#define INC_CONSTANTS_H

#include <linux/ioctl.h>

#define MODNAME "CHAR_DEV"

#define DEVICE_NAME "timed-messaging"  /* name assigned to the range of devices, nothing to do with device file name in /dev/ - not mandatory  */

#define NO (0)
#define YES (NO+1)

#define MINORS 8

#define SET_SEND_TIMEOUT _IOW('q', 1, unsigned long)
#define SET_RECV_TIMEOUT _IOW('q', 2, unsigned long)
#define REVOKE_DELAYED_MESSAGES _IO('q', 3)

#endif /* INC_CONSTANTS_H */