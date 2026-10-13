#pragma once
#include <cstdint>
namespace freeink {
void metalioPlusSetGray16Profile(uint8_t profile);
void metalioPlusSetGray16UiRefresh(int x, int y, int width, int height, uint8_t contentEvery, uint8_t smallEvery);
bool metalioPlusRestoreGray16Baseline(uint8_t* proxy);
}
