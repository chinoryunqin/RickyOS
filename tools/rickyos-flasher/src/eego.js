// EEGO A4 (ESP32-S3 N16R8, 768x552 UC8279C). Installed through the Read Pico path (same
// chip, flash size and RickyOS partition layout; policy.js S3_BOARDS): the board tag
// tells the images apart. It has no hardware-accepted factory layout, so any system
// that is not RickyOS gets the complete install. Updates come from eego/ota.json
// (COS ota/eego/ first, then the site); its images share firmware/.
export const EEGO = Object.freeze({ board: 'eego_a4', chipId: 9 });
export const EEGO_TAG = `CROSSPOINT-BOARD-V1:${EEGO.board};`;
export function isEegoRelease(release) { return release?.board === EEGO.board; }
