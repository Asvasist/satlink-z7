/**
 * @file status.h
 * @brief Common status codes shared by all SatLink-Z7 C components.
 *
 * @implements SRS-LIB-003
 */
#ifndef SATLINK_COMMON_STATUS_H
#define SATLINK_COMMON_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/** Result of a SatLink library call. Zero means success, negative values are errors. */
typedef enum
{
    SATLINK_OK = 0,           /**< Operation completed successfully. */
    SATLINK_ERR_NULL = -1,    /**< A required pointer argument was NULL. */
    SATLINK_ERR_RANGE = -2,   /**< An argument was outside its valid range. */
    SATLINK_ERR_STATE = -3,   /**< The object was not in a state that allows the call. */
    SATLINK_ERR_IO = -4,      /**< A bus or device access failed. */
    SATLINK_ERR_TIMEOUT = -5, /**< The device did not respond in time. */
    SATLINK_ERR_EMPTY = -6,   /**< Nothing to return (no frame, no message). */
    SATLINK_ERR_FULL = -7     /**< No room to accept the request (buffer or mailbox full). */
} satlink_status_t;

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_COMMON_STATUS_H */
