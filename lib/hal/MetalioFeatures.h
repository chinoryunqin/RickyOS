#pragma once

#include <BoardConfig.h>

// Physical board identity and optional firmware modules are separate. Defaults
// preserve the complete ordinary-board build; validation profiles can omit a
// peripheral without changing its board tag or the other device backends.
#ifndef RICKYOS_METALIO_CAMERA
#define RICKYOS_METALIO_CAMERA FREEINK_DEVICE_METALIO_EINK4
#endif
#ifndef RICKYOS_METALIO_CELLULAR
#define RICKYOS_METALIO_CELLULAR FREEINK_DEVICE_METALIO_EINK4
#endif
#if (RICKYOS_METALIO_CAMERA != 0 && RICKYOS_METALIO_CAMERA != 1) || \
    (RICKYOS_METALIO_CELLULAR != 0 && RICKYOS_METALIO_CELLULAR != 1)
#error "Metalio module switches must be 0 or 1"
#endif
#define RICKYOS_METALIO_CAMERA_ENABLED (FREEINK_DEVICE_METALIO_EINK4 && RICKYOS_METALIO_CAMERA)
#define RICKYOS_METALIO_CELLULAR_ENABLED (FREEINK_DEVICE_METALIO_EINK4 && RICKYOS_METALIO_CELLULAR)
