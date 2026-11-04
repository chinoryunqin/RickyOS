#include <cassert>

#include "HalCamera.h"
#include "HalCellular.h"
#include "MetalioFeatures.h"

int main() {
  static_assert(!RICKYOS_METALIO_CAMERA_ENABLED && !RICKYOS_METALIO_CELLULAR_ENABLED);
  // Link the actual HAL implementations with no USB/modem/FreeRTOS SDK headers
  // or objects. Disabled modules cannot create jobs or claim the USB PHY.
  HalCamera::recoverUsbBeforeSerial();
  HalCamera::begin("/images/camera");
  HalCamera::pump();
  assert(!HalCamera::capture() && !HalCamera::requestPreview() && !HalCamera::saveCurrent());
  assert(!HalCamera::busy() && !HalCamera::usbOwned() && !HalCamera::consumeRestartRequest());
  assert(HalCamera::frame().data == nullptr && HalCamera::frame().size == 0);
  HalCamera::leave();
  assert(HalCamera::prepareForSleep());
  HalCellular::begin();
  HalCellular::pump();
  assert(!HalCellular::enable(true) && !HalCellular::selectSim(0));
  assert(!HalCellular::refresh() && !HalCellular::probe());
  assert(!HalCellular::connected() && !HalCellular::busy());
  assert(HalCellular::snapshot().state == HalCellular::State::Off);
  assert(HalCellular::shutdown());
}
