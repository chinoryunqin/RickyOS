"""Resolve the build-side equivalent of lib/hal/MetalioFeatures.h."""


def modules_from_defines(defines):
    values = {}
    for item in defines:
        if isinstance(item, (tuple, list)):
            values[str(item[0])] = str(item[1])
        else:
            values[str(item)] = '1'
    board = values.get('FREEINK_DEVICE_METALIO_EINK4', '0') == '1'
    result = {}
    for module in ('camera', 'cellular'):
        key = 'RICKYOS_METALIO_' + module.upper()
        value = values.get(key, '1' if board else '0')
        if value not in ('0', '1'):
            raise ValueError(key + ' must be 0 or 1')
        result[module] = board and value == '1'
    return result
