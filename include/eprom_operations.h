#ifndef __EPROM_OPERATIONS_H__
#define __EPROM_OPERATIONS_H__
#include "firestarter.h"
#ifdef __cplusplus
extern "C" {
#endif

    bool eprom_read(firestarter_handle_t* handle);
    bool eprom_write(firestarter_handle_t* handle);
    bool eprom_erase(firestarter_handle_t* handle);
    bool eprom_check_chip_id(firestarter_handle_t* handle);
    bool eprom_blank_check(firestarter_handle_t* handle);
    bool eprom_sdp_unlock(firestarter_handle_t* handle);
    bool eprom_sdp_lock(firestarter_handle_t* handle);
    bool eprom_lock_status(firestarter_handle_t* handle);

#ifdef __cplusplus
}
#endif

#endif // __EPROM_OPERATIONS_H__