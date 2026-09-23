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
    SATLINK_OK = 0,         /**< Operation completed successfully. */
    SATLINK_ERR_NULL = -1,  /**< A required pointer argument was NULL. */
    SATLINK_ERR_RANGE = -2, /**< An argument was outside its valid range. */
    SATLINK_ERR_STATE = -3  /**< The object was not in a state that allows the call. */
} satlink_status_t;

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_COMMON_STATUS_H */
