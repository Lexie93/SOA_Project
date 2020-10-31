# Timed messaging system
Project for [Advanced Operating Systems 2019/2020](https://francescoquaglia.github.io/TEACHING/AOS/AA-2019-2020)

## Project specification
This specification is related to the implementation of a device file that allows exchanging messages across threads. Messages can be posted to the device file using common write() operations and can be acquired using common read() operations offered by the VFS API. To gain the possibility to read()/write() messages, threads must have access to a valid I/O session towards the device file. Each message posted to the device file is an independent data unit (a mail-slot message in the WinAPI analogy) and each read operation can extract the content of a single message (if present). Messages that are already stored in the device file must be delivered to readers in FIFO order. The message receipt fully invalidates the content of the message to be delivered to the user land buffer (so the message logically disappears from the device file, and is not re-deliverable), even if the read() operation requests less bytes than the current size of the message to be delivered. In addition, both write() and read() operations can be controlled by relying it the ioctl() interface, which must support a set of commands for defining the actual operating mode of read() and write() in an I/O session. In more detail, ioctl() can be used to post these commands:

   - SET_SEND_TIMEOUT, this command sets the current I/O session to a mode that does not directly stores the messages to the device file upon write() operations, rather they are stored after a timeout (expressed with granularity at least equal to the one of jiffies), however the write() operation immediately returns control to the calling thread. Clearly, timeout set to the value zero means immediate storing.
   - SET_RECV_TIMEOUT, this command sets the current I/O session to a mode that allows a thread calling a read() operation to resume its execution after a timeout (expressed with granularity at least equal to the one of jiffies) even if no message is currently present in (and is therefore deliverable from) the device file. Clearly, timeout set to zero means non-blocking reads in the absence of messages from the device file.
   - REVOKE_DELAYED_MESSAGES, this command allows undoing the message-post operations, caused by write(), occurred in a given session, of messages that have not yet been stored into the device file because their send-timeout (if any) is not yet expired. 

By default, a newly opened session has both the previous timeouts set to the value zero.

Also, the file operation flush() must be supported in order to reset the state of the device file, meaning that all threads waiting for messages (along any session) must be unblocked and all the delayed messages not yet delivered (along any session) must be revoked.

The driver of the device file must therefore support at least the following set of file operations:

   - open
   - release
   - read
   - write
   - unlocked_ioctl
   - flush 

Concurrent I/O sessions on the device file must be supported. Also, the device file can be multi-instance (so its driver must be able to handle different minor numbers, associated with the different instances, to be configured at compile time).

Finally, the device file handling software must expose via the /sys file system the following set of reconfigurable parameters:

   - max_message_size, the maximum size (bytes) currently allowed for posting messages to the device file
   - max_storage_size, the maximum number of bytes globally allowed for keeping messages in the device file, so that if a new message post (i.e., a write()) is requested and such maximum size is already met, then the post must fail 
