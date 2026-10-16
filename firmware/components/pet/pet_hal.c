/* pet_hal 的通用访问器：保存/取回当前安装的硬件实现。
 * 与具体板子无关；板级实现（pet_hal_sticks3.c）在启动时 install。 */
#include "pet_hal.h"

static const pet_hal_t *s_hal;

void pet_hal_install(const pet_hal_t *hal)
{
    s_hal = hal;
}

const pet_hal_t *pet_hal_get(void)
{
    return s_hal;
}

uint32_t pet_hal_now_ms(void)
{
    return (s_hal != NULL && s_hal->now_ms != NULL) ? s_hal->now_ms() : 0u;
}
