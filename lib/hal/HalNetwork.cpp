#include "HalNetwork.h"

#include <WiFi.h>

#include "HalCellular.h"
bool HalNetwork::connected() { return WiFi.status() == WL_CONNECTED || HalCellular::connected(); }
